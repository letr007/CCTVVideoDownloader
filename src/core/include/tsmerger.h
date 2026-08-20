#pragma once

#include <QObject>
#include <QDebug>
#include <QFile>
#include <array>
#include <functional>
#include <vector>

#ifdef CORE_REGRESSION_TESTS
void setTsMergerTestPacketProcessedHook(const std::function<void()>& hook);
void clearTsMergerTestPacketProcessedHook();
#endif

class TSMerger {
private:
	static const size_t TS_PACKET_SIZE = 188;
	static const uint8_t SYNC_BYTE = 0x47;

	uint16_t pmtPid = 0;
	bool pmtIdentified = false;
	std::array<int, 8192> continuityCounters{};
	std::array<int, 8192> continuityOffsets{};

public:
	bool merge(const std::vector<QString>& inputFiles, const QString& outputFile,
		const std::function<bool()>& cancellationRequested = {});

	void reset() {
		pmtPid = 0;
		pmtIdentified = false;
		continuityCounters.fill(-1);
		continuityOffsets.fill(-1);
	}
private:
	bool processFile(const QString& fileName,
		QFile& outFile,
		bool isFirstFile,
		const std::function<bool()>& cancellationRequested);
	void identifyPMTPID(const std::vector<uint8_t>& data, size_t packetOffset);
	bool normalizeContinuityCounter(std::vector<uint8_t>& data, size_t packetOffset);
	bool validatePacket(const std::vector<uint8_t>& data, size_t offset);
};
