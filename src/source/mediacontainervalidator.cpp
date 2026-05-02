#include "../head/mediacontainervalidator.h"

#include <QByteArray>
#include <QFile>

#include <array>

namespace {

constexpr qint64 MAX_SIGNATURE_BYTES = 64 * 1024;
constexpr int MIN_TS_SYNC_COUNT = 4;
constexpr std::array<int, 3> TS_PACKET_SIZES{ 188, 192, 204 };

bool hasTsSyncPattern(const QByteArray& data, int packetSize)
{
	if (data.size() < packetSize * MIN_TS_SYNC_COUNT) {
		return false;
	}

	for (int offset = 0; offset < packetSize; ++offset) {
		int syncCount = 0;
		for (int index = offset; index < data.size(); index += packetSize) {
			if (static_cast<unsigned char>(data.at(index)) != 0x47) {
				break;
			}
			++syncCount;
			if (syncCount >= MIN_TS_SYNC_COUNT) {
				return true;
			}
		}
	}

	return false;
}

bool looksLikeTransportStream(const QByteArray& data)
{
	for (const int packetSize : TS_PACKET_SIZES) {
		if (hasTsSyncPattern(data, packetSize)) {
			return true;
		}
	}

	return false;
}

quint32 readBigEndian32(const QByteArray& data, int offset)
{
	return (static_cast<quint32>(static_cast<unsigned char>(data.at(offset))) << 24)
		| (static_cast<quint32>(static_cast<unsigned char>(data.at(offset + 1))) << 16)
		| (static_cast<quint32>(static_cast<unsigned char>(data.at(offset + 2))) << 8)
		| static_cast<quint32>(static_cast<unsigned char>(data.at(offset + 3)));
}

bool hasEarlyFtypBox(const QByteArray& data)
{
	const int scanLimit = qMin(data.size(), 4096);
	for (int offset = 0; offset + 16 <= scanLimit; ) {
		const quint32 boxSize = readBigEndian32(data, offset);
		if (boxSize < 8) {
			return false;
		}

		const int nextOffset = offset + static_cast<int>(boxSize);
		if (nextOffset > data.size()) {
			return false;
		}

		if (data.mid(offset + 4, 4) == "ftyp") {
			return true;
		}

		offset = nextOffset;
	}

	return false;
}

bool isPlausibleBoxType(const QByteArray& data, int offset)
{
	if (offset + 4 > data.size()) {
		return false;
	}

	for (int i = 0; i < 4; ++i) {
		const unsigned char c = static_cast<unsigned char>(data.at(offset + i));
		if (c < 0x20 || c > 0x7E) {
			return false;
		}
	}

	return true;
}

bool hasPlausibleEarlyMp4Layout(const QByteArray& data)
{
	const int scanLimit = qMin(data.size(), 4096);
	int offset = 0;
	bool sawFtyp = false;

	while (offset + 8 <= scanLimit) {
		const quint32 boxSize = readBigEndian32(data, offset);
		if (boxSize < 8 || !isPlausibleBoxType(data, offset + 4)) {
			return false;
		}

		if (data.mid(offset + 4, 4) == "ftyp") {
			sawFtyp = true;
		}

		const qint64 nextOffset = static_cast<qint64>(offset) + boxSize;
		if (nextOffset > data.size()) {
			return sawFtyp;
		}

		offset = static_cast<int>(nextOffset);
		if (sawFtyp && offset == data.size()) {
			return true;
		}
	}

	return sawFtyp;
}

MediaContainerValidationResult failureResult(const QString& code,
	const QString& message,
	MediaContainerType expectedType = MediaContainerType::Unknown,
	MediaContainerType detectedType = MediaContainerType::Unknown)
{
	MediaContainerValidationResult result;
	result.expectedType = expectedType;
	result.detectedType = detectedType;
	result.code = code;
	result.message = message;
	return result;
}

}

MediaContainerValidationResult MediaContainerValidator::detectContainer(const QString& filePath)
{
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly)) {
		return failureResult(QStringLiteral("open_failed"),
			QStringLiteral("Unable to open media file: %1").arg(filePath));
	}

	const qint64 fileSize = file.size();
	if (fileSize <= 0) {
		return failureResult(QStringLiteral("empty_file"),
			QStringLiteral("Media file is empty: %1").arg(filePath));
	}

	const QByteArray signatureBytes = file.read(qMin(fileSize, MAX_SIGNATURE_BYTES));
	if (signatureBytes.isEmpty()) {
		return failureResult(QStringLiteral("read_failed"),
			QStringLiteral("Unable to read media file: %1").arg(filePath));
	}

	const bool hasTsSyncPattern = looksLikeTransportStream(signatureBytes);
	const bool hasFtypBox = hasEarlyFtypBox(signatureBytes);

	if (hasFtypBox && (!hasTsSyncPattern || hasPlausibleEarlyMp4Layout(signatureBytes))) {
		MediaContainerValidationResult result;
		result.ok = true;
		result.detectedType = MediaContainerType::Mp4;
		result.code = QStringLiteral("mp4_detected");
		result.message = QStringLiteral("Detected MP4 ftyp box");
		return result;
	}

	if (hasTsSyncPattern) {
		MediaContainerValidationResult result;
		result.ok = true;
		result.detectedType = MediaContainerType::MpegTs;
		result.code = QStringLiteral("mpeg_ts_detected");
		result.message = QStringLiteral("Detected MPEG-TS sync-byte pattern");
		return result;
	}

	return failureResult(QStringLiteral("unknown_container"),
		QStringLiteral("Unsupported or invalid media container"));
}

MediaContainerValidationResult MediaContainerValidator::validateFile(const QString& filePath, MediaContainerType expectedType)
{
	MediaContainerValidationResult result = detectContainer(filePath);
	result.expectedType = expectedType;

	if (!result.ok || expectedType == MediaContainerType::Unknown) {
		return result;
	}

	if (result.detectedType == expectedType) {
		result.code = QStringLiteral("container_match");
		result.message = QStringLiteral("Detected expected media container");
		return result;
	}

	return failureResult(QStringLiteral("unexpected_container"),
		QStringLiteral("Detected media container does not match expected type"),
		expectedType,
		result.detectedType);
}
