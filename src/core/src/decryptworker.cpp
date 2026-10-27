#include "../../include/decryptworker.h"
#include "../../include/mediafinalizer.h"
#include "../../include/mediacontainervalidator.h"

#include "crypto/cctv_h5e_decrypt.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>

#include <cstring>
#include <vector>

namespace {

int normalizeProcessTimeoutMs(int timeoutMs)
{
	return timeoutMs > 0 ? timeoutMs : 30000;
}

bool canCreateAndRemoveProbeFile(const QString& directoryPath)
{
	const QString probeFilePath = QDir(directoryPath).filePath(
		QStringLiteral(".decrypt_write_probe_%1.tmp").arg(QString::number(QCoreApplication::applicationPid()))
	);

	QFile probeFile(probeFilePath);
	if (!probeFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}

	if (probeFile.write("ok") != 2) {
		probeFile.close();
		QFile::remove(probeFilePath);
		return false;
	}

	probeFile.close();
	return QFile::remove(probeFilePath);
}

bool removeDirectory(const QString& path)
{
	qDebug() << "删除目录:" << path;

	QDir dir(path);
	if (!dir.exists()) {
		qDebug() << "目录不存在，无需删除";
		return true;
	}

	dir.setFilter(QDir::NoDotAndDotDot | QDir::AllEntries);
	const QFileInfoList entries = dir.entryInfoList();
	for (const QFileInfo& info : entries) {
		if (info.isDir()) {
			if (!removeDirectory(info.absoluteFilePath())) {
				return false;
			}
		} else if (!QFile::remove(info.absoluteFilePath())) {
			qWarning() << "删除文件失败:" << info.absoluteFilePath();
			return false;
		}
	}

	const bool result = QDir().rmdir(path);
	if (!result) {
		qWarning() << "删除目录失败:" << path;
	}
	return result;
}

QString cappedDiagnosticText(const QString& text)
{
	QString trimmed = text.trimmed();
	if (trimmed.size() > 2048) {
		trimmed.truncate(2048);
	}
	return trimmed;
}

QString processExitCodeLabel(int exitCode)
{
	if (exitCode >= 0) {
		return QString::number(exitCode);
	}

	return QStringLiteral("%1/0x%2")
		.arg(exitCode)
		.arg(QString::number(static_cast<quint32>(exitCode), 16).toUpper().rightJustified(8, QLatin1Char('0')));
}

QString preferredProcessDiagnostic(const DecryptProcessResult& result, const QString& diagnosticDirectory)
{
	const QString stderrText = cappedDiagnosticText(result.stderrText);
	if (!stderrText.isEmpty()) {
		return stderrText;
	}

	const QString stdoutText = cappedDiagnosticText(result.stdoutText);
	if (!stdoutText.isEmpty()) {
		return stdoutText;
	}

	QFile outputFile(QDir(diagnosticDirectory).filePath(QStringLiteral("output.txt")));
	if (outputFile.exists() && outputFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
		const QString fileText = cappedDiagnosticText(QString::fromLocal8Bit(outputFile.read(4096)));
		if (!fileText.isEmpty()) {
			return QStringLiteral("[output.txt] %1").arg(fileText);
		}
	}

	return QStringLiteral("native decrypt failed");
}

struct NativeDecryptResult
{
	bool ok = false;
	bool cancelled = false;
	QString code;
	QString message;
};

// 视频 PES 起点，用作分块边界，保证每块从完整 PES 开始。
std::vector<qint64> collectVideoPesStarts(QFile& file, uint16_t vpid)
{
	std::vector<qint64> starts;
	// 包对齐的扫描缓冲，顺序读避免 seek 与缓冲混用导致的错位。
	constexpr qint64 kScanBytes = 188 * 22000;
	QByteArray scan(static_cast<int>(kScanBytes), Qt::Uninitialized);
	file.seek(0);
	qint64 base = 0;
	qint64 got = 0;
	while ((got = file.read(scan.data(), kScanBytes)) > 0) {
		const auto* bytes = reinterpret_cast<const uint8_t*>(scan.constData());
		for (qint64 off = 0; off + 188 <= got; off += 188) {
			const uint8_t* packet = bytes + off;
			if (packet[0] != 0x47) {
				continue;
			}
			if ((((packet[1] & 0x1F) << 8) | packet[2]) != vpid) {
				continue;
			}
			if ((packet[1] & 0x40) == 0) {
				continue;
			}
			starts.push_back(base + off);
		}
		base += got;
	}
	return starts;
}

NativeDecryptResult decryptTsFileInPlace(const QString& tsPath,
	const std::function<bool()>& cancellationRequested)
{
	NativeDecryptResult result;

	if (cancellationRequested && cancellationRequested()) {
		result.cancelled = true;
		result.code = QStringLiteral("cancelled");
		result.message = QStringLiteral("cancelled");
		return result;
	}

	QFile file(tsPath);
	if (!file.open(QIODevice::ReadWrite)) {
		result.code = QStringLiteral("input_unreadable");
		result.message = QStringLiteral("无法读写加密 TS: %1").arg(tsPath);
		return result;
	}

	const qint64 fileSize = file.size();
	if (fileSize < 188) {
		result.code = QStringLiteral("input_empty");
		result.message = QStringLiteral("加密 TS 为空: %1").arg(tsPath);
		return result;
	}

	const std::vector<qint64> pesStarts = collectVideoPesStarts(file, 0x100);
	if (pesStarts.empty()) {
		result.code = QStringLiteral("native_decrypt_failed");
		result.message = QStringLiteral("未找到视频 PES: %1").arg(tsPath);
		return result;
	}

	// Chunk on PES boundaries so memory stays flat; the decryptor keeps the
	// byte length unchanged, which allows writing each chunk back in place.
	constexpr qint64 kChunkBytes = 64LL << 20;
	cctv_h5e::Session session;
	size_t nalCount = 0;
	std::vector<uint8_t> chunk;
	for (size_t index = 0; index < pesStarts.size();) {
		if (cancellationRequested && cancellationRequested()) {
			result.cancelled = true;
			result.code = QStringLiteral("cancelled");
			result.message = QStringLiteral("cancelled");
			return result;
		}

		const qint64 begin = pesStarts.at(index);
		size_t next = index + 1;
		while (next < pesStarts.size() && pesStarts.at(next) - begin < kChunkBytes) {
			++next;
		}
		const qint64 end = next < pesStarts.size() ? pesStarts.at(next) : fileSize;
		const size_t chunkLen = static_cast<size_t>(end - begin);

		chunk.resize(chunkLen);
		file.seek(begin);
		if (file.read(reinterpret_cast<char*>(chunk.data()), static_cast<qint64>(chunkLen)) != static_cast<qint64>(chunkLen)) {
			result.code = QStringLiteral("input_unreadable");
			result.message = QStringLiteral("读取加密 TS 失败: %1").arg(tsPath);
			return result;
		}

		const size_t before = chunk.size();
		nalCount += cctv_h5e::decrypt_ts_inplace(chunk.data(), chunk.size(), session, 0x100);
		if (chunk.size() != before) {
			result.code = QStringLiteral("native_decrypt_failed");
			result.message = QStringLiteral("原生 h5e 解密异常改变了 TS 长度");
			return result;
		}

		file.seek(begin);
		if (file.write(reinterpret_cast<const char*>(chunk.data()), static_cast<qint64>(chunkLen)) != static_cast<qint64>(chunkLen)) {
			result.code = QStringLiteral("output_write_failed");
			result.message = QStringLiteral("解密结果写入不完整");
			return result;
		}
		index = next;
	}

	if (nalCount == 0) {
		result.code = QStringLiteral("native_decrypt_failed");
		result.message = QStringLiteral("原生 h5e 解密失败（未处理任何视频 NAL）");
		return result;
	}
	qInfo() << "原生 h5e 解密完成, NAL 数:" << static_cast<qulonglong>(nalCount)
		<< "TS 字节:" << static_cast<qulonglong>(fileSize);

	result.ok = true;
	result.code = QStringLiteral("ok");
	result.message = QStringLiteral("native h5e decrypt ok");
	return result;
}

} // namespace

DecryptWorker::DecryptWorker(QObject* parent)
	: QObject(parent)
{
}

void DecryptWorker::cancelDecrypt()
{
	m_cancelled.store(true, std::memory_order_relaxed);
}

void DecryptWorker::setProcessTimeoutMs(int timeoutMs)
{
	m_processTimeoutMs = normalizeProcessTimeoutMs(timeoutMs);
}

void DecryptWorker::doDecrypt()
{
	qInfo() << "开始视频解密(native h5e) - 视频名称:" << m_name << "保存路径:" << m_savePath;

	auto cancellationRequested = [this]() {
		return m_cancelled.load(std::memory_order_relaxed);
	};

	if (cancellationRequested()) {
		emit decryptFinished(false, QStringLiteral("cancelled"));
		return;
	}

	const QString trimmedName = m_name.trimmed();
	const QString trimmedSavePath = m_savePath.trimmed();
	if (trimmedName.isEmpty() || trimmedSavePath.isEmpty()) {
		emit decryptFinished(false, QStringLiteral("解密失败 [code=invalid_params]: 解密参数无效"));
		return;
	}

	QFileInfo savePathInfo(trimmedSavePath);
	if (!savePathInfo.exists() || !savePathInfo.isDir()) {
		emit decryptFinished(false, QStringLiteral("解密失败 [code=output_unwritable]: 输出目录不可写"));
		return;
	}

	QString filePath = m_taskDirectory.trimmed();
	if (filePath.isEmpty()) {
		const auto nameHash = QString(
			QCryptographicHash::hash(m_name.toUtf8(), QCryptographicHash::Sha256).toHex()
		);
		filePath = QDir::cleanPath(trimmedSavePath + QLatin1Char('/') + nameHash);
	}
	qInfo() << "临时文件路径:" << filePath;

	QFileInfo tempDirectoryInfo(filePath);
	if (!tempDirectoryInfo.exists() || !tempDirectoryInfo.isDir()) {
		emit decryptFinished(false, QStringLiteral("解密失败 [code=input_missing]: result.ts 不存在"));
		return;
	}

	const QString stagingInputPath = QDir(filePath).filePath(QStringLiteral("result.ts"));
	const QString stagedDecryptedTsPath = stagingInputPath;
	qInfo() << "TS文件路径:" << stagingInputPath;

	QFileInfo stagingInfo(stagingInputPath);
	if (!stagingInfo.exists() || !stagingInfo.isFile()) {
		emit decryptFinished(false, QStringLiteral("解密失败 [code=input_missing]: result.ts 不存在"));
		return;
	}

	if (!canCreateAndRemoveProbeFile(trimmedSavePath)) {
		emit decryptFinished(false, QStringLiteral("解密失败 [code=output_unwritable]: 输出目录不可写"));
		return;
	}

	const MediaContainerType desiredContainer = m_transcodeToMp4
		? MediaContainerType::Mp4
		: MediaContainerType::MpegTs;

#ifdef CORE_REGRESSION_TESTS
	if (m_testProcessRunner) {
		// Keep historical test seam: inject a fake decrypt step around result.ts.
		DecryptProcessRequest request;
		request.program = QStringLiteral("native-h5e");
		request.arguments = {stagingInputPath, stagedDecryptedTsPath};
		request.workingDirectory = trimmedSavePath;
		request.timeoutMs = m_processTimeoutMs;
		request.cancellationRequested = cancellationRequested;

		const DecryptProcessResult processResult = m_testProcessRunner(request);
		if (processResult.cancelled || cancellationRequested()) {
			emit decryptFinished(false, QStringLiteral("cancelled"));
			return;
		}
		if (!processResult.started) {
			emit decryptFinished(false, processResult.errorString.isEmpty()
				? QStringLiteral("解密失败 [code=start_failed]: 无法启动解密")
				: QStringLiteral("解密失败 [code=start_failed]: 无法启动解密: %1").arg(processResult.errorString));
			return;
		}
		if (processResult.timedOut) {
			emit decryptFinished(false, QStringLiteral("解密失败 [code=timeout]: 解密超时 %1 ms").arg(request.timeoutMs));
			return;
		}
		if (processResult.exitCode != 0 || processResult.exitStatus != QProcess::NormalExit) {
			const QString diagnostic = preferredProcessDiagnostic(processResult, trimmedSavePath);
			emit decryptFinished(false, QStringLiteral("解密失败 [code=process_failed; exit_code=%1]: %2")
				.arg(processExitCodeLabel(processResult.exitCode))
				.arg(diagnostic));
			return;
		}
	} else
#endif
	{
		QElapsedTimer timer;
		timer.start();
		const NativeDecryptResult decryptResult = decryptTsFileInPlace(stagingInputPath, cancellationRequested);
		qInfo() << "原生 h5e 解密耗时:" << timer.elapsed() << "ms";

		if (decryptResult.cancelled || cancellationRequested()) {
			emit decryptFinished(false, QStringLiteral("cancelled"));
			return;
		}
		if (!decryptResult.ok) {
			emit decryptFinished(false, QStringLiteral("解密失败 [code=%1]: %2")
				.arg(decryptResult.code, decryptResult.message));
			return;
		}
	}

	const MediaContainerValidationResult validation =
		MediaContainerValidator::validateFile(stagedDecryptedTsPath, MediaContainerType::MpegTs);
	if (!validation.ok) {
		qCritical() << "解密输出不是有效的TS文件:" << validation.code << validation.message;
		emit decryptFinished(false, QStringLiteral("解密失败 [code=invalid_decrypt_output]: [%1] %2")
			.arg(validation.code, validation.message));
		return;
	}

	MediaFinalizer finalizer;
	finalizer.setProcessTimeoutMs(m_processTimeoutMs);
#ifdef CORE_REGRESSION_TESTS
	if (!m_testDecryptAssetsDir.isEmpty()) {
		finalizer.setTestDecryptAssetsDir(m_testDecryptAssetsDir);
	}
#endif
	if (cancellationRequested()) {
		emit decryptFinished(false, QStringLiteral("cancelled"));
		return;
	}

	const MediaFinalizeResult finalizeResult = finalizer.finalize(stagedDecryptedTsPath,
		m_name,
		trimmedSavePath,
		desiredContainer,
		m_replaceExisting,
		cancellationRequested);
	if (!finalizeResult.ok) {
		if (finalizeResult.code == QStringLiteral("cancelled") || cancellationRequested()) {
			emit decryptFinished(false, QStringLiteral("cancelled"));
			return;
		}

		qCritical() << "MediaFinalizer 发布失败:" << finalizeResult.code << finalizeResult.message;
		emit decryptFinished(false, QStringLiteral("解密失败 [code=%1]: %2")
			.arg(finalizeResult.code, finalizeResult.message));
		return;
	}

	const QString finalPath = finalizeResult.finalPath;
	qInfo() << "最终文件路径:" << finalPath;
	if (QFile::exists(stagedDecryptedTsPath) && !QFile::remove(stagedDecryptedTsPath)) {
		qWarning() << "清理解密阶段 result.ts 失败:" << stagedDecryptedTsPath;
	}

	if (!removeDirectory(filePath)) {
		qWarning() << "移除临时文件夹失败，请手动清理:" << filePath;
		emit decryptFinished(false, QStringLiteral("移除临时文件夹失败\n请手动清理"));
		return;
	}

	qInfo() << "视频解密全部完成，输出文件:" << finalPath;
	emit decryptFinished(true, QStringLiteral("解密完成，输出 ") + m_name);
}

#ifdef CORE_REGRESSION_TESTS
void DecryptWorker::setTestProcessRunner(const std::function<DecryptProcessResult(const DecryptProcessRequest&)>& runner)
{
	m_testProcessRunner = runner;
}

void DecryptWorker::clearTestProcessRunner()
{
	m_testProcessRunner = {};
}

void DecryptWorker::setTestDecryptAssetsDir(const QString& decryptAssetsDir)
{
	m_testDecryptAssetsDir = decryptAssetsDir;
}

void DecryptWorker::clearTestDecryptAssetsDir()
{
	m_testDecryptAssetsDir.clear();
}
#endif
