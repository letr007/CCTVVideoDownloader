#include "../head/directmediafinalizer.h"

#include "../head/mediafinalizer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>

DirectMediaFinalizeResult finalizeDirectTsTask(const QString& title,
	const QString& savePath,
	bool transcodeToMp4,
	const QString& taskDirectory,
	const std::function<bool()>& cancellationRequested
#ifdef CORE_REGRESSION_TESTS
	,
	const std::function<FfmpegCliProcessResult(const FfmpegCliProcessRequest&)>& testProcessRunner,
	const QString& testDecryptAssetsDir
#endif
	)
{
	DirectMediaFinalizeResult result;
	if (cancellationRequested && cancellationRequested()) {
		result.code = QStringLiteral("cancelled");
		result.message = QStringLiteral("cancelled");
		return result;
	}

	const QString rawTitle = title;
	const QString trimmedTitle = title.trimmed();
	const QString trimmedSavePath = savePath.trimmed();
	if (trimmedTitle.isEmpty() || trimmedSavePath.isEmpty()) {
		result.code = QStringLiteral("invalid_params");
		result.message = QStringLiteral("Direct finalizer parameters are invalid");
		return result;
	}

	QString taskDirPath = taskDirectory.trimmed();
	if (taskDirPath.isEmpty()) {
		const QString nameHash = QString(
			QCryptographicHash::hash(rawTitle.toUtf8(), QCryptographicHash::Sha256).toHex()
		);
		taskDirPath = QDir::cleanPath(trimmedSavePath + "/" + nameHash);
	}
	const QString stagingTsPath = QDir(taskDirPath).filePath(QStringLiteral("result.ts"));
	const MediaContainerType desiredContainer = transcodeToMp4
		? MediaContainerType::Mp4
		: MediaContainerType::MpegTs;

	MediaFinalizer finalizer;
	#ifdef CORE_REGRESSION_TESTS
	if (testProcessRunner) {
		finalizer.setTestProcessRunner(testProcessRunner);
	}
	if (!testDecryptAssetsDir.isEmpty()) {
		finalizer.setTestDecryptAssetsDir(testDecryptAssetsDir);
	}
	#endif
	const MediaFinalizeResult finalizeResult = finalizer.finalize(stagingTsPath,
		trimmedTitle,
		trimmedSavePath,
		desiredContainer,
		cancellationRequested);
	if (!finalizeResult.ok) {
		result.code = finalizeResult.code;
		result.message = finalizeResult.message;
		return result;
	}

	if (!QDir(taskDirPath).removeRecursively()) {
		result.code = QStringLiteral("temp_cleanup_failed");
		result.message = QStringLiteral("Unable to remove temporary direct-download directory: %1").arg(taskDirPath);
		result.finalPath = finalizeResult.finalPath;
		return result;
	}

	QFile::remove(QDir(trimmedSavePath).filePath(QStringLiteral("output.txt")));

	result.ok = true;
	result.code = finalizeResult.code;
	result.message = finalizeResult.message;
	result.finalPath = finalizeResult.finalPath;
	return result;
}

void DirectFinalizeWorker::cancelFinalize()
{
	m_cancelled.store(true, std::memory_order_relaxed);
}

void DirectFinalizeWorker::doWork(const QString& title, const QString& savePath, bool transcodeToMp4)
{
	if (m_cancelled.load(std::memory_order_relaxed)) {
		emit finished(false, QStringLiteral("cancelled"), QStringLiteral("cancelled"), QString());
		return;
	}

	const DirectMediaFinalizeResult result = finalizeDirectTsTask(title,
		savePath,
		transcodeToMp4,
		m_taskDirectory,
		[this]() { return m_cancelled.load(std::memory_order_relaxed); }
#ifdef CORE_REGRESSION_TESTS
		,
		m_testProcessRunner,
		m_testDecryptAssetsDir
#endif
	);
	emit finished(result.ok, result.code, result.message, result.finalPath);
}

#ifdef CORE_REGRESSION_TESTS
void DirectFinalizeWorker::setTestProcessRunner(const std::function<FfmpegCliProcessResult(const FfmpegCliProcessRequest&)>& runner)
{
	m_testProcessRunner = runner;
}

void DirectFinalizeWorker::clearTestProcessRunner()
{
	m_testProcessRunner = {};
}

void DirectFinalizeWorker::setTestDecryptAssetsDir(const QString& decryptAssetsDir)
{
	m_testDecryptAssetsDir = decryptAssetsDir;
}

void DirectFinalizeWorker::clearTestDecryptAssetsDir()
{
	m_testDecryptAssetsDir.clear();
}
#endif
