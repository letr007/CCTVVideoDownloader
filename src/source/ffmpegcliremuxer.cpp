#include "../head/ffmpegcliremuxer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QElapsedTimer>

namespace {

int normalizeProcessTimeoutMs(int timeoutMs)
{
	return timeoutMs > 0 ? timeoutMs : 30000;
}

QString cappedDiagnosticText(const QString& text)
{
	QString trimmed = text.trimmed();
	if (trimmed.size() > 2048) {
		trimmed.truncate(2048);
	}
	return trimmed;
}

QString preferredProcessDiagnostic(const FfmpegCliProcessResult& result)
{
	const QString stderrText = cappedDiagnosticText(result.stderrText);
	if (!stderrText.isEmpty()) {
		return stderrText;
	}

	const QString stdoutText = cappedDiagnosticText(result.stdoutText);
	if (!stdoutText.isEmpty()) {
		return stdoutText;
	}

	return QStringLiteral("ffmpeg 退出失败");
}

bool isCancellationRequested(const std::function<bool()>& cancellationRequested)
{
	return cancellationRequested && cancellationRequested();
}

FfmpegCliProcessResult runFfmpegProcess(const FfmpegCliProcessRequest& request)
{
	QProcess ffmpeg;
	ffmpeg.setWorkingDirectory(request.workingDirectory);
	ffmpeg.start(request.program, request.arguments);

	FfmpegCliProcessResult result;
	QElapsedTimer timer;
	timer.start();
	constexpr int pollIntervalMs = 50;
	while (!ffmpeg.waitForStarted(pollIntervalMs)) {
		if (isCancellationRequested(request.cancellationRequested)) {
			result.cancelled = true;
			ffmpeg.terminate();
			if (!ffmpeg.waitForFinished(1000) && ffmpeg.state() != QProcess::NotRunning) {
				ffmpeg.kill();
				ffmpeg.waitForFinished(1000);
			}
			result.stdoutText = QString::fromLocal8Bit(ffmpeg.readAllStandardOutput());
			result.stderrText = QString::fromLocal8Bit(ffmpeg.readAllStandardError());
			result.errorString = ffmpeg.errorString();
			result.exitCode = ffmpeg.exitCode();
			result.exitStatus = ffmpeg.exitStatus();
			return result;
		}

		if (ffmpeg.error() != QProcess::UnknownError || timer.elapsed() >= request.timeoutMs) {
			result.errorString = ffmpeg.errorString();
			return result;
		}
	}

	result.started = true;
	timer.restart();
	while (!ffmpeg.waitForFinished(pollIntervalMs)) {
		if (isCancellationRequested(request.cancellationRequested)) {
			result.cancelled = true;
			result.stdoutText = QString::fromLocal8Bit(ffmpeg.readAllStandardOutput());
			result.stderrText = QString::fromLocal8Bit(ffmpeg.readAllStandardError());
			result.errorString = ffmpeg.errorString();

			ffmpeg.terminate();
			if (!ffmpeg.waitForFinished(1000) && ffmpeg.state() != QProcess::NotRunning) {
				ffmpeg.kill();
				ffmpeg.waitForFinished(1000);
			}

			result.exitCode = ffmpeg.exitCode();
			result.exitStatus = ffmpeg.exitStatus();
			result.errorString = ffmpeg.errorString();
			return result;
		}

		if (timer.elapsed() >= request.timeoutMs) {
			result.timedOut = true;
			result.stdoutText = QString::fromLocal8Bit(ffmpeg.readAllStandardOutput());
			result.stderrText = QString::fromLocal8Bit(ffmpeg.readAllStandardError());
			result.errorString = ffmpeg.errorString();

			ffmpeg.terminate();
			if (!ffmpeg.waitForFinished(1000) && ffmpeg.state() != QProcess::NotRunning) {
				ffmpeg.kill();
				ffmpeg.waitForFinished(1000);
			}

			result.exitCode = ffmpeg.exitCode();
			result.exitStatus = ffmpeg.exitStatus();
			result.errorString = ffmpeg.errorString();
			return result;
		}
	}

	result.exitCode = ffmpeg.exitCode();
	result.exitStatus = ffmpeg.exitStatus();
	result.stdoutText = QString::fromLocal8Bit(ffmpeg.readAllStandardOutput());
	result.stderrText = QString::fromLocal8Bit(ffmpeg.readAllStandardError());
	result.errorString = ffmpeg.errorString();
	return result;
}

FfmpegCliRemuxResult failureResult(const QString& code, const QString& message)
{
	FfmpegCliRemuxResult result;
	result.code = code;
	result.message = message;
	return result;
}

int computeRemuxTimeoutMs(const QString& inputPath, int baseTimeoutMs)
{
	const QFileInfo info(inputPath);
	if (!info.exists())
		return baseTimeoutMs;

	constexpr qint64 bytesPerChunk = 16LL * 1024 * 1024; // 16 MiB
	const qint64 fileSize = info.size();
	const qint64 chunks = (fileSize + bytesPerChunk - 1) / bytesPerChunk;
	const qint64 computedMs = chunks * 1000;

	constexpr qint64 intMax = 2147483647LL;
	const qint64 clampedMs = computedMs < intMax ? computedMs : intMax;
	return baseTimeoutMs > static_cast<int>(clampedMs) ? baseTimeoutMs : static_cast<int>(clampedMs);
}

} // namespace

void FfmpegCliRemuxer::setProcessTimeoutMs(int timeoutMs)
{
	m_processTimeoutMs = normalizeProcessTimeoutMs(timeoutMs);
}

QString FfmpegCliRemuxer::decryptAssetsDir() const
{
#ifdef CORE_REGRESSION_TESTS
	if (!m_testDecryptAssetsDir.isEmpty()) {
		return m_testDecryptAssetsDir;
	}
#endif

	return QDir(QCoreApplication::applicationDirPath()).filePath("decrypt");
}

FfmpegCliRemuxResult FfmpegCliRemuxer::remuxTsToMp4(const QString& inputTsPath,
	const QString& outputMp4TempPath,
	const std::function<bool()>& cancellationRequested) const
{
	const QString trimmedInputPath = inputTsPath.trimmed();
	const QString trimmedOutputPath = outputMp4TempPath.trimmed();
	if (isCancellationRequested(cancellationRequested)) {
		return failureResult(QStringLiteral("cancelled"), QStringLiteral("cancelled"));
	}

	if (trimmedInputPath.isEmpty() || trimmedOutputPath.isEmpty()) {
		return failureResult(QStringLiteral("invalid_params"),
			QStringLiteral("FFmpeg remux parameters are invalid"));
	}

	const QFileInfo inputInfo(trimmedInputPath);
	if (!inputInfo.exists() || !inputInfo.isFile()) {
		return failureResult(QStringLiteral("input_missing"),
			QStringLiteral("Staging TS file does not exist: %1").arg(trimmedInputPath));
	}

	const QFileInfo outputInfo(trimmedOutputPath);
	if (!QDir().mkpath(outputInfo.absolutePath())) {
		return failureResult(QStringLiteral("output_unwritable"),
			QStringLiteral("Unable to create output directory: %1").arg(outputInfo.absolutePath()));
	}

	const QString ffmpegPath = QDir(decryptAssetsDir()).filePath("ffmpeg.exe");
	const QFileInfo ffmpegInfo(ffmpegPath);
	if (!ffmpegInfo.exists() || !ffmpegInfo.isFile()) {
		return failureResult(QStringLiteral("ffmpeg_missing"),
			QStringLiteral("decrypt/ffmpeg.exe does not exist"));
	}

	if (QFile::exists(trimmedOutputPath) && !QFile::remove(trimmedOutputPath)) {
		return failureResult(QStringLiteral("temp_cleanup_failed"),
			QStringLiteral("Unable to remove stale remux output: %1").arg(trimmedOutputPath));
	}

	FfmpegCliProcessRequest request;
	request.program = ffmpegPath;
	request.arguments = {
		QStringLiteral("-hide_banner"),
		QStringLiteral("-y"),
		QStringLiteral("-i"),
		trimmedInputPath,
		QStringLiteral("-c"),
		QStringLiteral("copy"),
		QStringLiteral("-f"),
		QStringLiteral("mp4"),
		trimmedOutputPath
	};
	request.workingDirectory = outputInfo.absolutePath();
	request.timeoutMs = computeRemuxTimeoutMs(trimmedInputPath, m_processTimeoutMs);
	request.cancellationRequested = cancellationRequested;
	qInfo() << "启动FFmpeg封装进程，超时:" << request.timeoutMs << "ms, 参数:" << request.arguments;

	FfmpegCliProcessResult processResult;
#ifdef CORE_REGRESSION_TESTS
	if (m_testProcessRunner) {
		processResult = m_testProcessRunner(request);
	}
	else {
		processResult = runFfmpegProcess(request);
	}
#else
	processResult = runFfmpegProcess(request);
#endif
	qInfo() << "FFmpeg stdout:" << cappedDiagnosticText(processResult.stdoutText);
	qInfo() << "FFmpeg stderr:" << cappedDiagnosticText(processResult.stderrText);

	if (processResult.cancelled || isCancellationRequested(cancellationRequested)) {
		FfmpegCliRemuxResult result = failureResult(QStringLiteral("cancelled"), QStringLiteral("cancelled"));
		result.processResult = processResult;
		return result;
	}

	if (!processResult.started) {
		FfmpegCliRemuxResult result = failureResult(QStringLiteral("start_failed"),
			cappedDiagnosticText(processResult.errorString).isEmpty()
				? QStringLiteral("Unable to start ffmpeg.exe")
				: QStringLiteral("Unable to start ffmpeg.exe: %1").arg(cappedDiagnosticText(processResult.errorString)));
		result.processResult = processResult;
		return result;
	}

	if (processResult.timedOut) {
		FfmpegCliRemuxResult result = failureResult(QStringLiteral("timeout"),
			QStringLiteral("ffmpeg.exe timed out after %1 ms").arg(request.timeoutMs));
		result.processResult = processResult;
		return result;
	}

	if (processResult.exitCode != 0 || processResult.exitStatus != QProcess::NormalExit) {
		FfmpegCliRemuxResult result = failureResult(QStringLiteral("process_failed"),
			preferredProcessDiagnostic(processResult));
		result.processResult = processResult;
		return result;
	}

	FfmpegCliRemuxResult result;
	result.ok = true;
	result.code = QStringLiteral("ok");
	result.message = QStringLiteral("FFmpeg CLI remux completed");
	result.outputPath = trimmedOutputPath;
	result.processResult = processResult;
	qInfo() << "FFmpeg封装成功完成，退出码:" << processResult.exitCode;
	return result;
}

#ifdef CORE_REGRESSION_TESTS
void FfmpegCliRemuxer::setTestProcessRunner(const std::function<FfmpegCliProcessResult(const FfmpegCliProcessRequest&)>& runner)
{
	m_testProcessRunner = runner;
}

void FfmpegCliRemuxer::clearTestProcessRunner()
{
	m_testProcessRunner = nullptr;
}

void FfmpegCliRemuxer::setTestDecryptAssetsDir(const QString& decryptAssetsDir)
{
	m_testDecryptAssetsDir = decryptAssetsDir;
}

void FfmpegCliRemuxer::clearTestDecryptAssetsDir()
{
	m_testDecryptAssetsDir.clear();
}
#endif
