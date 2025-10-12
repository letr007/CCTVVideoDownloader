#pragma once

#include <QObject>
#include <QDebug>
#include <vector>

class TSMerger {
private:
	static const size_t TS_PACKET_SIZE = 188;
	static const uint8_t SYNC_BYTE = 0x47;

	uint16_t pmtPid = 0;
	bool pmtIdentified = false;

public:
	bool merge(const std::vector<QString>& inputFiles, const QString& outputFile);

	void reset() {
		pmtPid = 0;
		pmtIdentified = false;
	}
private:
	bool processFile(const QString& fileName, std::ofstream& outFile, bool isFirstFile);
	void identifyPMTPID(const std::vector<uint8_t>& data, size_t packetOffset);
	bool validatePacket(const std::vector<uint8_t>& data, size_t offset);
};
