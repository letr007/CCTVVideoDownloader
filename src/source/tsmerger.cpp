#include "../head/tsmerger.h"

#include <fstream>

bool TSMerger::merge(const std::vector<QString>& inputFiles, const QString& outputFile) {
    qInfo() << "开始合并TS文件，输入文件数:" << inputFiles.size() << "输出文件:" << outputFile;
    
    if (inputFiles.empty()) {
        qWarning() << "没有输入文件可合并";
        return false;
    }
    std::ofstream outfile(outputFile.toStdString(), std::ios::binary);
    if (!outfile) {
        qCritical() << "无法打开输出文件:" << outputFile;
        return false;
    }
    qInfo() << "输出文件已打开";

    size_t totalPackets = 0;
    for (size_t i=0; i < inputFiles.size(); ++i) {
        qInfo() << "处理第" << (i+1) << "/" << inputFiles.size() << "个文件:" << inputFiles[i];
        if (!processFile(inputFiles[i], outfile, i == 0)) {
            qCritical() << "处理文件失败:" << inputFiles[i];
            outfile.close();
            return false;
        }
        qInfo() << "第" << (i+1) << "个文件处理完成";
    }

    outfile.close();
    qInfo() << "所有文件合并完成，输出文件:" << outputFile;

    if (!pmtIdentified) {
        qWarning() << "合并完成，但未识别到PMT";
    } else {
        qInfo() << "PMT识别成功，PID:" << pmtPid;
    }
    
    return true;
}

bool TSMerger::processFile(const QString& filename, std::ofstream& outfile, bool isFirstFile) {
    qDebug() << "处理TS文件:" << filename << "是否为第一个文件:" << isFirstFile;
    
    std::ifstream infile(filename.toStdString(), std::ios::binary);
    if (!infile) {
        qCritical() << "无法打开输入文件:" << filename;
        return false;
    }

    // 获取文件大小
    infile.seekg(0, std::ios::end);
    size_t fileSize = infile.tellg();
    infile.seekg(0, std::ios::beg);

    qDebug() << "文件大小:" << fileSize << "字节";

    if (fileSize % TS_PACKET_SIZE != 0) {
        qWarning() << "文件大小不是TS包的整数倍:" << filename << "大小:" << fileSize;
        //return false;
    }

    // 读取整个文件
    std::vector<uint8_t> data(fileSize);
    infile.read(reinterpret_cast<char*>(data.data()), fileSize);

    if (!infile) {
        qCritical() << "读取文件失败:" << filename;
        return false;
    }

    size_t packetCount = 0;
    size_t skippedCount = 0;
    size_t invalidCount = 0;

    for (size_t i = 0; i + TS_PACKET_SIZE <= data.size(); i += TS_PACKET_SIZE) {
        if (!validatePacket(data, i)) {
            invalidCount++;
            continue; // 跳过无效包
        }

        uint16_t pid = (data[i + 1] & 0x1F) << 8 | data[i + 2];

        if (isFirstFile) {
            if (pid == 0 && !pmtIdentified) {
                identifyPMTPID(data, i);
            }
            outfile.write(reinterpret_cast<char*>(&data[i]), TS_PACKET_SIZE);
            packetCount++;
        }
        else {
            if (pid == 0 || (pmtIdentified && pid == pmtPid)) {
                skippedCount++;
                continue; // 跳过PAT和PMT包
            }
            outfile.write(reinterpret_cast<char*>(&data[i]), TS_PACKET_SIZE);
            packetCount++;
        }
    }

    if (invalidCount > 0) {
        qWarning() << "跳过" << invalidCount << "个无效TS包";
    }
    if (skippedCount > 0) {
        qDebug() << "跳过" << skippedCount << "个PAT/PMT包";
    }
    
    qDebug() << "文件处理完成:" << filename << "有效包数:" << packetCount;
    return true;
}

bool TSMerger::validatePacket(const std::vector<uint8_t>& data, size_t offset) {
    if (data[offset] != SYNC_BYTE) {
        qDebug() << "TS包同步字节错误，偏移:" << offset;
        return false; // 同步字节错误
    }
    return true;
}

void TSMerger::identifyPMTPID(const std::vector<uint8_t>& data, size_t offset) {
    if (data.size() >= offset + 20) {
        // PAT 包中，第13-14字节包含第一个节目的 PMT_PID
        pmtPid = ((data[offset + 13] & 0x1F) << 8) |
            data[offset + 14];
        pmtIdentified = true;

        qDebug() << "识别到PMT PID:" << pmtPid;
    } else {
        qWarning() << "无法识别PMT PID: 数据长度不足";
    }
}