#include "../head/mediafinalizer.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace {

MediaFinalizeResult failureResult(const QString& code,
	const QString& message,
	MediaContainerType publishedType = MediaContainerType::Unknown)
{
	MediaFinalizeResult result;
	result.code = code;
	result.message = message;
	result.publishedType = publishedType;
	return result;
}

QString validationFailureMessage(const QString& prefix, const MediaContainerValidationResult& validation)
{
	return QStringLiteral("%1 [%2]: %3").arg(prefix, validation.code, validation.message);
}

bool isCancellationRequested(const std::function<bool()>& cancellationRequested)
{
	return cancellationRequested && cancellationRequested();
}

} // namespace

void MediaFinalizer::setProcessTimeoutMs(int timeoutMs)
{
	m_remuxer.setProcessTimeoutMs(timeoutMs);
}

#ifdef CORE_REGRESSION_TESTS
void MediaFinalizer::setTestProcessRunner(const std::function<FfmpegCliProcessResult(const FfmpegCliProcessRequest&)>& runner)
{
	m_remuxer.setTestProcessRunner(runner);
}

void MediaFinalizer::setTestDecryptAssetsDir(const QString& decryptAssetsDir)
{
	m_remuxer.setTestDecryptAssetsDir(decryptAssetsDir);
}
#endif

MediaFinalizeResult MediaFinalizer::finalize(const QString& stagingTsPath,
	const QString& title,
	const QString& saveDir,
	MediaContainerType desiredContainer,
	const std::function<bool()>& cancellationRequested)
{
	if (isCancellationRequested(cancellationRequested)) {
		return failureResult(QStringLiteral("cancelled"), QStringLiteral("cancelled"), desiredContainer);
	}

	const QString trimmedStagingPath = stagingTsPath.trimmed();
	const QString trimmedTitle = title.trimmed();
	const QString trimmedSaveDir = saveDir.trimmed();
	if (trimmedStagingPath.isEmpty() || trimmedTitle.isEmpty() || trimmedSaveDir.isEmpty()) {
		return failureResult(QStringLiteral("invalid_params"),
			QStringLiteral("Media finalizer parameters are invalid"));
	}

	if (desiredContainer != MediaContainerType::MpegTs && desiredContainer != MediaContainerType::Mp4) {
		return failureResult(QStringLiteral("unsupported_container"),
			QStringLiteral("Unsupported final media container"));
	}

	const QFileInfo saveDirInfo(trimmedSaveDir);
	if (!saveDirInfo.exists() || !saveDirInfo.isDir()) {
		return failureResult(QStringLiteral("output_unwritable"),
			QStringLiteral("Final output directory does not exist: %1").arg(trimmedSaveDir),
			desiredContainer);
	}

	const MediaContainerValidationResult stagingValidation =
		MediaContainerValidator::validateFile(trimmedStagingPath, MediaContainerType::MpegTs);
	if (!stagingValidation.ok) {
		return failureResult(QStringLiteral("invalid_staging_ts"),
			validationFailureMessage(QStringLiteral("Staging TS validation failed"), stagingValidation),
			desiredContainer);
	}

	const QString baseName = sanitizedTitle(trimmedTitle);
	if (baseName.isEmpty()) {
		return failureResult(QStringLiteral("invalid_title"),
			QStringLiteral("Final media title is invalid after sanitization"),
			desiredContainer);
	}

	if (desiredContainer == MediaContainerType::MpegTs) {
		const QString finalTsPath = uniqueOutputPath(trimmedSaveDir, baseName, QStringLiteral("ts"));
		if (isCancellationRequested(cancellationRequested)) {
			return failureResult(QStringLiteral("cancelled"), QStringLiteral("cancelled"), MediaContainerType::MpegTs);
		}

		if (!QFile::rename(trimmedStagingPath, finalTsPath)) {
			return failureResult(QStringLiteral("publish_failed"),
				QStringLiteral("Unable to publish final TS file: %1").arg(finalTsPath),
				MediaContainerType::MpegTs);
		}

		if (isCancellationRequested(cancellationRequested)) {
			QFile::remove(finalTsPath);
			return failureResult(QStringLiteral("cancelled"), QStringLiteral("cancelled"), MediaContainerType::MpegTs);
		}

		MediaFinalizeResult result;
		result.ok = true;
		result.code = QStringLiteral("published_ts");
		result.message = QStringLiteral("Published validated MPEG-TS file");
		result.finalPath = finalTsPath;
		result.publishedType = MediaContainerType::MpegTs;
		return result;
	}

	const QString finalMp4Path = uniqueOutputPath(trimmedSaveDir, baseName, QStringLiteral("mp4"));
	const QString tempMp4Path = finalMp4Path + QStringLiteral(".tmp");
	qInfo() << "开始MP4封装，输入TS:" << trimmedStagingPath << "临时输出:" << tempMp4Path;
	if (QFile::exists(tempMp4Path) && !QFile::remove(tempMp4Path)) {
		return failureResult(QStringLiteral("temp_cleanup_failed"),
			QStringLiteral("Unable to clear stale MP4 temp file: %1").arg(tempMp4Path),
			MediaContainerType::Mp4);
	}

	const FfmpegCliRemuxResult remuxResult = m_remuxer.remuxTsToMp4(trimmedStagingPath, tempMp4Path, cancellationRequested);
	if (!remuxResult.ok) {
		if (QFile::exists(tempMp4Path)) {
			QFile::remove(tempMp4Path);
		}
		if (remuxResult.code == QStringLiteral("cancelled") || isCancellationRequested(cancellationRequested)) {
			return failureResult(QStringLiteral("cancelled"), QStringLiteral("cancelled"), MediaContainerType::Mp4);
		}
		return failureResult(remuxResult.code,
			QStringLiteral("MP4 remux failed: %1").arg(remuxResult.message),
			MediaContainerType::Mp4);
	}

	const MediaContainerValidationResult mp4Validation =
		MediaContainerValidator::validateFile(tempMp4Path, MediaContainerType::Mp4);
	if (!mp4Validation.ok) {
		QFile::remove(tempMp4Path);
		return failureResult(QStringLiteral("invalid_remuxed_mp4"),
			validationFailureMessage(QStringLiteral("Remuxed MP4 validation failed"), mp4Validation),
			MediaContainerType::Mp4);
	}
	qInfo() << "MP4封装校验通过:" << tempMp4Path;

	if (isCancellationRequested(cancellationRequested)) {
		QFile::remove(tempMp4Path);
		return failureResult(QStringLiteral("cancelled"), QStringLiteral("cancelled"), MediaContainerType::Mp4);
	}

	if (!QFile::rename(tempMp4Path, finalMp4Path)) {
		QFile::remove(tempMp4Path);
		return failureResult(QStringLiteral("publish_failed"),
			QStringLiteral("Unable to publish final MP4 file: %1").arg(finalMp4Path),
			MediaContainerType::Mp4);
	}

	if (isCancellationRequested(cancellationRequested)) {
		QFile::remove(finalMp4Path);
		return failureResult(QStringLiteral("cancelled"), QStringLiteral("cancelled"), MediaContainerType::Mp4);
	}

	MediaFinalizeResult result;
	result.ok = true;
	result.code = QStringLiteral("published_mp4");
	result.message = QStringLiteral("Published validated MP4 file");
	result.finalPath = finalMp4Path;
	result.publishedType = MediaContainerType::Mp4;
	return result;
}

QString MediaFinalizer::sanitizedTitle(const QString& title) const
{
	QString sanitized = title;
	sanitized.replace(QRegularExpression(R"([\\/:*?"<>|])"), QStringLiteral("_"));
	return sanitized.trimmed();
}

QString MediaFinalizer::uniqueOutputPath(const QString& saveDir, const QString& baseName, const QString& suffix) const
{
	QString outputPath = QDir(saveDir).filePath(QStringLiteral("%1.%2").arg(baseName, suffix));
	QFileInfo fileInfo(outputPath);
	const QString completeBaseName = fileInfo.completeBaseName();
	const QString fileSuffix = fileInfo.suffix();
	const QString path = fileInfo.path();

	int counter = 1;
	while (QFile::exists(outputPath) && counter <= 1000) {
		outputPath = QDir(path).filePath(
			QStringLiteral("%1(%2).%3").arg(completeBaseName).arg(counter).arg(fileSuffix)
		);
		++counter;
	}

	return outputPath;
}
