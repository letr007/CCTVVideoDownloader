#include "../head/directmediafinalizer.h"

#include "../head/mediafinalizer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>

DirectMediaFinalizeResult finalizeDirectTsTask(const QString& title,
	const QString& savePath,
	bool transcodeToMp4)
{
	DirectMediaFinalizeResult result;
	const QString rawTitle = title;
	const QString trimmedTitle = title.trimmed();
	const QString trimmedSavePath = savePath.trimmed();
	if (trimmedTitle.isEmpty() || trimmedSavePath.isEmpty()) {
		result.code = QStringLiteral("invalid_params");
		result.message = QStringLiteral("Direct finalizer parameters are invalid");
		return result;
	}

	const QString nameHash = QString(
		QCryptographicHash::hash(rawTitle.toUtf8(), QCryptographicHash::Sha256).toHex()
	);
	const QString taskDirPath = QDir::cleanPath(trimmedSavePath + "/" + nameHash);
	const QString stagingTsPath = QDir(taskDirPath).filePath(QStringLiteral("result.ts"));
	const MediaContainerType desiredContainer = transcodeToMp4
		? MediaContainerType::Mp4
		: MediaContainerType::MpegTs;

	MediaFinalizer finalizer;
	const MediaFinalizeResult finalizeResult = finalizer.finalize(stagingTsPath,
		trimmedTitle,
		trimmedSavePath,
		desiredContainer);
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

void DirectFinalizeWorker::doWork(const QString& title, const QString& savePath, bool transcodeToMp4)
{
	const DirectMediaFinalizeResult result = finalizeDirectTsTask(title, savePath, transcodeToMp4);
	emit finished(result.ok, result.code, result.message, result.finalPath);
}
