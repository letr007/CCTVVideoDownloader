#include "../head/libavremuxer.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QtLogging>

#include <algorithm>
#include <limits>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavcodec/codec_par.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

namespace {

constexpr int kMinRemuxTimeoutMs = 5000;
constexpr int kMaxRemuxTimeoutMs = 30 * 60 * 1000;
constexpr qint64 kAssumedRemuxBytesPerSecond = 2 * 1024 * 1024;

bool isCancellationRequested(const std::function<bool()>& cancellationRequested)
{
	return cancellationRequested && cancellationRequested();
}

int normalizeProcessTimeoutMs(int timeoutMs)
{
	if (timeoutMs <= 0) {
		return 30000;
	}

	return std::clamp(timeoutMs, 1, std::numeric_limits<int>::max() - 1);
}

int computeRemuxTimeoutMs(const QString& inputTsPath, int configuredTimeoutMs)
{
	const int normalizedConfiguredTimeoutMs = normalizeProcessTimeoutMs(configuredTimeoutMs);
	const QFileInfo inputInfo(inputTsPath);
	if (!inputInfo.exists() || !inputInfo.isFile()) {
		return normalizedConfiguredTimeoutMs;
	}

	const qint64 fileSize = inputInfo.size();
	if (fileSize <= 0) {
		return normalizedConfiguredTimeoutMs;
	}

	const qint64 estimatedMs = (fileSize * 1000) / kAssumedRemuxBytesPerSecond;
	const qint64 scaledMs = estimatedMs * 3;
	const qint64 withFloor = std::max<qint64>(scaledMs, kMinRemuxTimeoutMs);
	const qint64 withConfiguredFloor = std::max<qint64>(withFloor, normalizedConfiguredTimeoutMs);
	const qint64 clamped = std::min<qint64>(withConfiguredFloor, kMaxRemuxTimeoutMs);
	return static_cast<int>(clamped);
}

QString cappedDiagnosticText(const QString& text, int maxChars = 1200)
{
	const QString trimmed = text.trimmed();
	if (trimmed.size() <= maxChars) {
		return trimmed;
	}

	return trimmed.left(maxChars) + QStringLiteral("...(truncated)");
}

QString avErrorString(int err)
{
	char buf[AV_ERROR_MAX_STRING_SIZE];
	av_strerror(err, buf, sizeof(buf));
	return QString::fromUtf8(buf);
}

LibavRemuxResult failureResult(const QString& code, const QString& message)
{
	LibavRemuxResult result;
	result.ok = false;
	result.code = code;
	result.message = message;
	return result;
}

struct StreamMapEntry
{
	int outIndex = -1;
	AVBSFContext* bsf = nullptr;
};

struct OutputTimestampState
{
	int64_t lastMuxDts = AV_NOPTS_VALUE;
	int64_t lastPacketDuration = 0;
};

bool checkedTimestampAdd(int64_t left, int64_t right, int64_t* result)
{
	if ((right > 0 && left > std::numeric_limits<int64_t>::max() - right)
		|| (right < 0 && left < std::numeric_limits<int64_t>::min() - right)) {
		return false;
	}
	*result = left + right;
	return true;
}

int failTimestampNormalization(QString* errorOut, int streamIndex, const char* stage, const QString& detail)
{
	if (errorOut != nullptr) {
		*errorOut = QStringLiteral("timestamp normalization failed (stream %1, stage %2): %3")
								.arg(streamIndex)
								.arg(QString::fromLatin1(stage))
								.arg(detail);
	}
	return AVERROR(EINVAL);
}

int normalizeAndWritePacket(AVFormatContext* ofmt,
	AVPacket* pkt,
	std::vector<OutputTimestampState>* timestampStates,
	QString* errorOut,
	const char* stage)
{
	if (pkt->stream_index < 0 || pkt->stream_index >= static_cast<int>(timestampStates->size())) {
		return failTimestampNormalization(errorOut, pkt->stream_index, stage,
			QStringLiteral("output stream index is out of range"));
	}

	OutputTimestampState& state = (*timestampStates)[static_cast<size_t>(pkt->stream_index)];
	if (pkt->pts == AV_NOPTS_VALUE && pkt->dts == AV_NOPTS_VALUE) {
		if (state.lastMuxDts == AV_NOPTS_VALUE) {
			return failTimestampNormalization(errorOut, pkt->stream_index, stage,
				QStringLiteral("first packet has no PTS or DTS"));
		}

		const int64_t duration = std::max<int64_t>(state.lastPacketDuration, 1);
		int64_t inferredTimestamp = 0;
		if (!checkedTimestampAdd(state.lastMuxDts, duration, &inferredTimestamp)) {
			return failTimestampNormalization(errorOut, pkt->stream_index, stage,
				QStringLiteral("PTS/DTS inference overflow"));
		}
		pkt->pts = inferredTimestamp;
		pkt->dts = inferredTimestamp;
	} else if (pkt->pts == AV_NOPTS_VALUE) {
		pkt->pts = pkt->dts;
	} else if (pkt->dts == AV_NOPTS_VALUE) {
		pkt->dts = pkt->pts;
	}

	if (pkt->dts > pkt->pts) {
		int64_t nextMuxDts = 0;
		if (!checkedTimestampAdd(state.lastMuxDts, 1, &nextMuxDts)) {
			return failTimestampNormalization(errorOut, pkt->stream_index, stage,
				QStringLiteral("DTS/PTS median-rule threshold overflow"));
		}

		// Equivalent to ffmpeg_mux.c's FFMIN3/FFMAX3 three-value median rule.
		const int64_t correctedTimestamp = std::max(std::min(pkt->pts, pkt->dts),
			std::min(std::max(pkt->pts, pkt->dts), nextMuxDts));
		pkt->pts = correctedTimestamp;
		pkt->dts = correctedTimestamp;
	}

	if (state.lastMuxDts != AV_NOPTS_VALUE && pkt->dts <= state.lastMuxDts) {
		int64_t nextMuxDts = 0;
		if (!checkedTimestampAdd(state.lastMuxDts, 1, &nextMuxDts)) {
			return failTimestampNormalization(errorOut, pkt->stream_index, stage,
				QStringLiteral("monotonic DTS correction overflow"));
		}
		if (pkt->pts >= pkt->dts) {
			pkt->pts = std::max(pkt->pts, nextMuxDts);
		}
		pkt->dts = nextMuxDts;
	}

	// av_interleaved_write_frame() takes ownership of the packet on success, so
	// preserve the normalized timing before the call clears/unrefs its fields.
	const int streamIndex = pkt->stream_index;
	const int64_t writtenDts = pkt->dts;
	const int64_t writtenDuration = pkt->duration;
	const int writeRet = av_interleaved_write_frame(ofmt, pkt);
	if (writeRet < 0) {
		if (errorOut != nullptr) {
			*errorOut = QStringLiteral("write packet failed (stream %1, stage %2): %3")
								.arg(streamIndex)
								.arg(QString::fromLatin1(stage))
								.arg(avErrorString(writeRet));
		}
		return writeRet;
	}

	state.lastMuxDts = writtenDts;
	state.lastPacketDuration = writtenDuration;
	return 0;
}

void freeStreamMaps(std::vector<StreamMapEntry>& maps)
{
	for (StreamMapEntry& entry : maps) {
		if (entry.bsf != nullptr) {
			av_bsf_free(&entry.bsf);
		}
	}
	maps.clear();
}

int openAacBsf(AVStream* inStream, AVBSFContext** outBsf)
{
	const AVBitStreamFilter* filter = av_bsf_get_by_name("aac_adtstoasc");
	if (filter == nullptr) {
		return AVERROR_BSF_NOT_FOUND;
	}

	AVBSFContext* bsf = nullptr;
	int ret = av_bsf_alloc(filter, &bsf);
	if (ret < 0) {
		return ret;
	}

	ret = avcodec_parameters_copy(bsf->par_in, inStream->codecpar);
	if (ret < 0) {
		av_bsf_free(&bsf);
		return ret;
	}
	bsf->time_base_in = inStream->time_base;

	ret = av_bsf_init(bsf);
	if (ret < 0) {
		av_bsf_free(&bsf);
		return ret;
	}

	*outBsf = bsf;
	return 0;
}

int remuxWithLibav(const QString& inputPath,
	const QString& outputPath,
	int timeoutMs,
	const std::function<bool()>& cancellationRequested,
	QString* errorOut)
{
	AVFormatContext* ifmt = nullptr;
	AVFormatContext* ofmt = nullptr;
	AVPacket* pkt = nullptr;
	std::vector<StreamMapEntry> streamMap;
	std::vector<OutputTimestampState> timestampStates;
	int ret = 0;
	QElapsedTimer timer;
	timer.start();

	const QByteArray inputUtf8 = inputPath.toUtf8();
	const QByteArray outputUtf8 = outputPath.toUtf8();

	auto fail = [&](int err, const QString& prefix) {
		if (errorOut != nullptr) {
			*errorOut = prefix;
			if (err < 0) {
				*errorOut += QStringLiteral(": ") + avErrorString(err);
			}
		}
		return err < 0 ? err : AVERROR_EXTERNAL;
	};

	ret = avformat_open_input(&ifmt, inputUtf8.constData(), nullptr, nullptr);
	if (ret < 0) {
		return fail(ret, QStringLiteral("open input failed"));
	}

	ret = avformat_find_stream_info(ifmt, nullptr);
	if (ret < 0) {
		avformat_close_input(&ifmt);
		return fail(ret, QStringLiteral("find stream info failed"));
	}

	ret = avformat_alloc_output_context2(&ofmt, nullptr, "mp4", outputUtf8.constData());
	if (ret < 0 || ofmt == nullptr) {
		avformat_close_input(&ifmt);
		return fail(ret < 0 ? ret : AVERROR_UNKNOWN, QStringLiteral("alloc output failed"));
	}

	streamMap.assign(ifmt->nb_streams, StreamMapEntry{});
	int outStreamIndex = 0;
	for (unsigned i = 0; i < ifmt->nb_streams; ++i) {
		AVStream* inStream = ifmt->streams[i];
		const AVCodecParameters* inPar = inStream->codecpar;
		streamMap[i].outIndex = -1;

		if (inPar->codec_type != AVMEDIA_TYPE_VIDEO
			&& inPar->codec_type != AVMEDIA_TYPE_AUDIO
			&& inPar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
			continue;
		}

		AVStream* outStream = avformat_new_stream(ofmt, nullptr);
		if (outStream == nullptr) {
			ret = AVERROR(ENOMEM);
			goto cleanup;
		}

		ret = avcodec_parameters_copy(outStream->codecpar, inPar);
		if (ret < 0) {
			goto cleanup;
		}
		outStream->codecpar->codec_tag = 0;
		outStream->time_base = inStream->time_base;

		// ADTS AAC in MPEG-TS must be converted for MP4.
		if (inPar->codec_type == AVMEDIA_TYPE_AUDIO && inPar->codec_id == AV_CODEC_ID_AAC) {
			ret = openAacBsf(inStream, &streamMap[i].bsf);
			if (ret < 0) {
				goto cleanup;
			}
			ret = avcodec_parameters_copy(outStream->codecpar, streamMap[i].bsf->par_out);
			if (ret < 0) {
				goto cleanup;
			}
			outStream->codecpar->codec_tag = 0;
		}

		streamMap[i].outIndex = outStreamIndex++;
	}

	if (outStreamIndex == 0) {
		ret = fail(AVERROR_STREAM_NOT_FOUND, QStringLiteral("no media streams found"));
		goto cleanup;
	}
	timestampStates.resize(static_cast<size_t>(outStreamIndex));

	if ((ofmt->oformat->flags & AVFMT_NOFILE) == 0) {
		ret = avio_open(&ofmt->pb, outputUtf8.constData(), AVIO_FLAG_WRITE);
		if (ret < 0) {
			goto cleanup;
		}
	}

	{
		AVDictionary* opts = nullptr;
		av_dict_set(&opts, "movflags", "faststart", 0);
		ret = avformat_write_header(ofmt, &opts);
		av_dict_free(&opts);
		if (ret < 0) {
			goto cleanup;
		}
	}

	pkt = av_packet_alloc();
	if (pkt == nullptr) {
		ret = AVERROR(ENOMEM);
		goto cleanup;
	}

	while (true) {
		if (isCancellationRequested(cancellationRequested)) {
			ret = AVERROR_EXIT;
			if (errorOut != nullptr) {
				*errorOut = QStringLiteral("cancelled");
			}
			goto cleanup;
		}
		if (timer.elapsed() > timeoutMs) {
			ret = AVERROR(ETIMEDOUT);
			if (errorOut != nullptr) {
				*errorOut = QStringLiteral("libav remux timed out after %1 ms").arg(timeoutMs);
			}
			goto cleanup;
		}

		ret = av_read_frame(ifmt, pkt);
		if (ret == AVERROR_EOF) {
			ret = 0;
			break;
		}
		if (ret < 0) {
			goto cleanup;
		}

		if (pkt->stream_index < 0 || pkt->stream_index >= static_cast<int>(streamMap.size())) {
			av_packet_unref(pkt);
			continue;
		}

		const StreamMapEntry& map = streamMap[static_cast<size_t>(pkt->stream_index)];
		if (map.outIndex < 0) {
			av_packet_unref(pkt);
			continue;
		}

		AVStream* inStream = ifmt->streams[pkt->stream_index];
		AVStream* outStream = ofmt->streams[map.outIndex];

		if (map.bsf != nullptr) {
			ret = av_bsf_send_packet(map.bsf, pkt);
			av_packet_unref(pkt);
			if (ret < 0) {
				goto cleanup;
			}

			while (ret >= 0) {
				ret = av_bsf_receive_packet(map.bsf, pkt);
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
					ret = 0;
					break;
				}
				if (ret < 0) {
					goto cleanup;
				}

				pkt->stream_index = map.outIndex;
				av_packet_rescale_ts(pkt, map.bsf->time_base_out, outStream->time_base);
				pkt->pos = -1;
				ret = normalizeAndWritePacket(ofmt, pkt, &timestampStates, errorOut, "aac_bsf_receive");
				av_packet_unref(pkt);
				if (ret < 0) {
					goto cleanup;
				}
			}
			continue;
		}

		pkt->stream_index = map.outIndex;
		av_packet_rescale_ts(pkt, inStream->time_base, outStream->time_base);
		pkt->pos = -1;
		ret = normalizeAndWritePacket(ofmt, pkt, &timestampStates, errorOut, "direct");
		av_packet_unref(pkt);
		if (ret < 0) {
			goto cleanup;
		}
	}

	// Flush bitstream filters.
	for (StreamMapEntry& map : streamMap) {
		if (map.bsf == nullptr || map.outIndex < 0) {
			continue;
		}
		ret = av_bsf_send_packet(map.bsf, nullptr);
		if (ret < 0) {
			goto cleanup;
		}
		while (true) {
			ret = av_bsf_receive_packet(map.bsf, pkt);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				ret = 0;
				break;
			}
			if (ret < 0) {
				goto cleanup;
			}
			AVStream* outStream = ofmt->streams[map.outIndex];
			pkt->stream_index = map.outIndex;
			av_packet_rescale_ts(pkt, map.bsf->time_base_out, outStream->time_base);
			pkt->pos = -1;
			ret = normalizeAndWritePacket(ofmt, pkt, &timestampStates, errorOut, "aac_bsf_flush");
			av_packet_unref(pkt);
			if (ret < 0) {
				goto cleanup;
			}
		}
	}

	ret = av_write_trailer(ofmt);
	if (ret < 0) {
		goto cleanup;
	}

cleanup:
	if (pkt != nullptr) {
		av_packet_free(&pkt);
	}
	freeStreamMaps(streamMap);
	if (ifmt != nullptr) {
		avformat_close_input(&ifmt);
	}
	if (ofmt != nullptr) {
		if ((ofmt->oformat->flags & AVFMT_NOFILE) == 0 && ofmt->pb != nullptr) {
			avio_closep(&ofmt->pb);
		}
		avformat_free_context(ofmt);
	}

	if (ret < 0 && errorOut != nullptr && errorOut->isEmpty()) {
		*errorOut = avErrorString(ret);
	}
	return ret;
}

} // namespace

void LibavRemuxer::setProcessTimeoutMs(int timeoutMs)
{
	m_processTimeoutMs = normalizeProcessTimeoutMs(timeoutMs);
}

LibavRemuxResult LibavRemuxer::remuxTsToMp4(const QString& inputTsPath,
	const QString& outputMp4TempPath,
	const std::function<bool()>& cancellationRequested) const
{
	const QString trimmedInputPath = inputTsPath.trimmed();
	const QString trimmedOutputPath = outputMp4TempPath.trimmed();
	if (trimmedInputPath.isEmpty() || trimmedOutputPath.isEmpty()) {
		return failureResult(QStringLiteral("invalid_params"), QStringLiteral("input/output path is empty"));
	}

	const QFileInfo inputInfo(trimmedInputPath);
	if (!inputInfo.exists() || !inputInfo.isFile()) {
		return failureResult(QStringLiteral("input_missing"),
			QStringLiteral("input ts does not exist: %1").arg(trimmedInputPath));
	}

	const QFileInfo outputInfo(trimmedOutputPath);
	if (outputInfo.absolutePath().trimmed().isEmpty()) {
		return failureResult(QStringLiteral("invalid_params"), QStringLiteral("output path is invalid"));
	}

	if (isCancellationRequested(cancellationRequested)) {
		return failureResult(QStringLiteral("cancelled"), QStringLiteral("cancelled"));
	}

	const int timeoutMs = computeRemuxTimeoutMs(trimmedInputPath, m_processTimeoutMs);

	// Synthetic request retained for regression tests that inject remux outcomes.
	RemuxProcessRequest request;
	request.program = QStringLiteral("libav-remux");
	request.arguments = {
		QStringLiteral("-y"),
		QStringLiteral("-i"),
		trimmedInputPath,
		QStringLiteral("-c"),
		QStringLiteral("copy"),
		QStringLiteral("-movflags"),
		QStringLiteral("+faststart"),
		trimmedOutputPath,
	};
	request.workingDirectory = outputInfo.absolutePath();
	request.timeoutMs = timeoutMs;
	request.cancellationRequested = cancellationRequested;

#ifdef CORE_REGRESSION_TESTS
	if (m_testProcessRunner) {
		qInfo() << "使用测试注入的 remux runner, 超时:" << request.timeoutMs << "ms";
		const RemuxProcessResult processResult = m_testProcessRunner(request);

		if (processResult.cancelled || isCancellationRequested(cancellationRequested)) {
			LibavRemuxResult result = failureResult(QStringLiteral("cancelled"), QStringLiteral("cancelled"));
			result.processResult = processResult;
			return result;
		}
		if (!processResult.started) {
			LibavRemuxResult result = failureResult(QStringLiteral("start_failed"),
				cappedDiagnosticText(processResult.errorString).isEmpty()
					? QStringLiteral("Unable to start libav remux")
					: QStringLiteral("Unable to start libav remux: %1")
						.arg(cappedDiagnosticText(processResult.errorString)));
			result.processResult = processResult;
			return result;
		}
		if (processResult.timedOut) {
			LibavRemuxResult result = failureResult(QStringLiteral("timeout"),
				QStringLiteral("libav remux timed out after %1 ms").arg(request.timeoutMs));
			result.processResult = processResult;
			return result;
		}
		if (processResult.exitCode != 0 || processResult.exitStatus != QProcess::NormalExit) {
			LibavRemuxResult result = failureResult(QStringLiteral("process_failed"),
				cappedDiagnosticText(processResult.stderrText).isEmpty()
					? cappedDiagnosticText(processResult.stdoutText)
					: cappedDiagnosticText(processResult.stderrText));
			if (result.message.isEmpty()) {
				result.message = QStringLiteral("libav remux failed");
			}
			result.processResult = processResult;
			return result;
		}

		LibavRemuxResult result;
		result.ok = true;
		result.code = QStringLiteral("ok");
		result.message = QStringLiteral("libav remux completed (test runner)");
		result.outputPath = trimmedOutputPath;
		result.processResult = processResult;
		return result;
	}
#endif

	qInfo() << "开始 libav 封装 TS->MP4, 超时:" << timeoutMs << "ms"
		<< "输入:" << trimmedInputPath
		<< "输出:" << trimmedOutputPath;

	QElapsedTimer timer;
	timer.start();
	QString remuxError;
	const int remuxCode = remuxWithLibav(trimmedInputPath, trimmedOutputPath, timeoutMs, cancellationRequested, &remuxError);
	qInfo() << "libav 封装耗时:" << timer.elapsed() << "ms, code:" << remuxCode;

	RemuxProcessResult processResult;
	processResult.started = true;
	processResult.exitStatus = QProcess::NormalExit;

	if (isCancellationRequested(cancellationRequested) || remuxError == QStringLiteral("cancelled")) {
		processResult.cancelled = true;
		processResult.exitCode = -1;
		LibavRemuxResult result = failureResult(QStringLiteral("cancelled"), QStringLiteral("cancelled"));
		result.processResult = processResult;
		return result;
	}

	if (remuxCode == AVERROR(ETIMEDOUT) || remuxError.contains(QStringLiteral("timed out"))) {
		processResult.timedOut = true;
		processResult.exitCode = -1;
		LibavRemuxResult result = failureResult(QStringLiteral("timeout"),
			remuxError.isEmpty()
				? QStringLiteral("libav remux timed out after %1 ms").arg(timeoutMs)
				: remuxError);
		result.processResult = processResult;
		return result;
	}

	if (remuxCode < 0) {
		processResult.exitCode = remuxCode;
		processResult.stderrText = remuxError;
		processResult.errorString = remuxError;
		// Clean partial output.
		if (QFile::exists(trimmedOutputPath)) {
			QFile::remove(trimmedOutputPath);
		}
		LibavRemuxResult result = failureResult(QStringLiteral("process_failed"),
			cappedDiagnosticText(remuxError).isEmpty()
				? QStringLiteral("libav remux failed")
				: cappedDiagnosticText(remuxError));
		result.processResult = processResult;
		return result;
	}

	if (!QFileInfo::exists(trimmedOutputPath) || QFileInfo(trimmedOutputPath).size() <= 0) {
		LibavRemuxResult result = failureResult(QStringLiteral("process_failed"),
			QStringLiteral("libav remux produced empty output"));
		result.processResult = processResult;
		return result;
	}

	processResult.exitCode = 0;
	LibavRemuxResult result;
	result.ok = true;
	result.code = QStringLiteral("ok");
	result.message = QStringLiteral("libav remux completed");
	result.outputPath = trimmedOutputPath;
	result.processResult = processResult;
	qInfo() << "libav 封装成功完成:" << trimmedOutputPath;
	return result;
}

#ifdef CORE_REGRESSION_TESTS
void LibavRemuxer::setTestProcessRunner(const std::function<RemuxProcessResult(const RemuxProcessRequest&)>& runner)
{
	m_testProcessRunner = runner;
}

void LibavRemuxer::clearTestProcessRunner()
{
	m_testProcessRunner = nullptr;
}

void LibavRemuxer::setTestDecryptAssetsDir(const QString& decryptAssetsDir)
{
	m_testDecryptAssetsDir = decryptAssetsDir;
}

void LibavRemuxer::clearTestDecryptAssetsDir()
{
	m_testDecryptAssetsDir.clear();
}
#endif
