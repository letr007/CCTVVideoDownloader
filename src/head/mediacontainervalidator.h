#pragma once

#include <QString>

enum class MediaContainerType
{
	Unknown,
	MpegTs,
	Mp4
};

struct MediaContainerValidationResult
{
	bool ok = false;
	MediaContainerType expectedType = MediaContainerType::Unknown;
	MediaContainerType detectedType = MediaContainerType::Unknown;
	QString code;
	QString message;
};

class MediaContainerValidator
{
public:
	static MediaContainerValidationResult detectContainer(const QString& filePath);
	static MediaContainerValidationResult validateFile(const QString& filePath, MediaContainerType expectedType);
};
