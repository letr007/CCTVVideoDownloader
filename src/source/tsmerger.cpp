#include "../head/tsmerger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {

bool isCancellationRequested(const std::function<bool()>& cancellationRequested)
{
    return cancellationRequested && cancellationRequested();
}

#ifdef CORE_REGRESSION_TESTS
std::function<void()>& tsMergerTestPacketProcessedHook()
{
    static std::function<void()> hook;
    return hook;
}

void invokeTsMergerTestPacketProcessedHook()
{
    auto& hook = tsMergerTestPacketProcessedHook();
    if (hook) {
        hook();
    }
}
#else
void invokeTsMergerTestPacketProcessedHook()
{
}
#endif

} // namespace

#ifdef CORE_REGRESSION_TESTS
void setTsMergerTestPacketProcessedHook(const std::function<void()>& hook)
{
    tsMergerTestPacketProcessedHook() = hook;
}

void clearTsMergerTestPacketProcessedHook()
{
    tsMergerTestPacketProcessedHook() = {};
}
#endif

bool TSMerger::merge(const std::vector<QString>& inputFiles,
    const QString& outputFile,
    const std::function<bool()>& cancellationRequested) {
    qInfo() << "开始合并TS文件，输入文件数:" << inputFiles.size() << "输出文件:" << outputFile;
    
    if (inputFiles.empty()) {
        qWarning() << "没有输入文件可合并";
        return false;
    }
    const QFileInfo outputInfo(outputFile);
    if (!QDir().mkpath(outputInfo.absolutePath())) {
        qCritical() << "无法创建输出目录:" << outputInfo.absolutePath();
        return false;
    }

    const QString tempOutputFile = outputFile + QStringLiteral(".merge_tmp");
    const QString backupOutputFile = outputFile + QStringLiteral(".merge_backup");

    if (QFile::exists(tempOutputFile) && !QFile::remove(tempOutputFile)) {
        qCritical() << "无法清理旧的临时合并输出文件:" << tempOutputFile;
        return false;
    }

    QFile outfile(tempOutputFile);
    if (!outfile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCritical() << "无法打开临时输出文件:" << tempOutputFile;
        return false;
    }
    qInfo() << "临时输出文件已打开:" << tempOutputFile;

    for (size_t i=0; i < inputFiles.size(); ++i) {
        if (isCancellationRequested(cancellationRequested)) {
            qInfo() << "TS合并已取消，停止处理后续文件";
            outfile.close();
            if (QFile::exists(tempOutputFile) && !QFile::remove(tempOutputFile)) {
                qWarning() << "取消后清理临时合并输出文件失败:" << tempOutputFile;
            }
            return false;
        }

        qInfo() << "处理第" << (i+1) << "/" << inputFiles.size() << "个文件:" << inputFiles[i];
        if (!processFile(inputFiles[i], outfile, i == 0, cancellationRequested)) {
            qCritical() << "处理文件失败:" << inputFiles[i];
            outfile.close();
            if (QFile::exists(tempOutputFile) && !QFile::remove(tempOutputFile)) {
                qWarning() << "清理失败的临时合并输出文件失败:" << tempOutputFile;
            }
            return false;
        }
        qInfo() << "第" << (i+1) << "个文件处理完成";
    }

    outfile.close();

    if (isCancellationRequested(cancellationRequested)) {
        qInfo() << "TS合并已取消，停止发布输出文件";
        if (QFile::exists(tempOutputFile) && !QFile::remove(tempOutputFile)) {
            qWarning() << "取消后清理临时合并输出文件失败:" << tempOutputFile;
        }
        return false;
    }

    const bool hadExistingOutput = QFile::exists(outputFile);
    bool movedExistingOutputToBackup = false;

    if (hadExistingOutput) {
        if (QFile::exists(backupOutputFile) && !QFile::remove(backupOutputFile)) {
            qCritical() << "无法清理旧的合并备份文件:" << backupOutputFile;
            if (QFile::exists(tempOutputFile) && !QFile::remove(tempOutputFile)) {
                qWarning() << "清理临时合并输出文件失败:" << tempOutputFile;
            }
            return false;
        }

        if (!QFile::rename(outputFile, backupOutputFile)) {
            qCritical() << "无法备份现有输出文件:" << outputFile << "->" << backupOutputFile;
            if (QFile::exists(tempOutputFile) && !QFile::remove(tempOutputFile)) {
                qWarning() << "清理临时合并输出文件失败:" << tempOutputFile;
            }
            return false;
        }

        movedExistingOutputToBackup = true;
    }

    if (!QFile::rename(tempOutputFile, outputFile)) {
        qCritical() << "无法发布合并输出文件:" << tempOutputFile << "->" << outputFile;
        if (movedExistingOutputToBackup && !QFile::rename(backupOutputFile, outputFile)) {
            qCritical() << "恢复原始输出文件失败:" << backupOutputFile << "->" << outputFile;
        }
        if (QFile::exists(tempOutputFile) && !QFile::remove(tempOutputFile)) {
            qWarning() << "清理未发布的临时合并输出文件失败:" << tempOutputFile;
        }
        return false;
    }

    if (movedExistingOutputToBackup && QFile::exists(backupOutputFile) && !QFile::remove(backupOutputFile)) {
        qWarning() << "清理合并备份文件失败:" << backupOutputFile;
    }

    qInfo() << "所有文件合并完成，输出文件:" << outputFile;

    if (!pmtIdentified) {
        qWarning() << "合并完成，但未识别到PMT";
    } else {
        qInfo() << "PMT识别成功，PID:" << pmtPid;
    }
    
    return true;
}

bool TSMerger::processFile(const QString& filename,
    QFile& outfile,
    bool isFirstFile,
    const std::function<bool()>& cancellationRequested) {
    qDebug() << "处理TS文件:" << filename << "是否为第一个文件:" << isFirstFile;

    if (isCancellationRequested(cancellationRequested)) {
        qInfo() << "TS文件处理前检测到取消请求:" << filename;
        return false;
    }
    
    QFile infile(filename);
    if (!infile.open(QIODevice::ReadOnly)) {
        qCritical() << "无法打开输入文件:" << filename;
        return false;
    }

    const qint64 fileSize = infile.size();

    qDebug() << "文件大小:" << fileSize << "字节";

    if (fileSize == 0) {
        qCritical() << "文件大小为0，无法处理:" << filename;
        return false;
    }

    if (fileSize % TS_PACKET_SIZE != 0) {
        qWarning() << "文件大小不是TS包的整数倍:" << filename << "大小:" << fileSize;
        //return false;
    }

    // 读取整个文件
    const QByteArray fileBytes = infile.readAll();
    if (fileBytes.size() != fileSize) {
        qCritical() << "读取文件失败:" << filename;
        return false;
    }
    std::vector<uint8_t> data(fileBytes.begin(), fileBytes.end());

    size_t packetCount = 0;
    size_t skippedCount = 0;
    size_t invalidCount = 0;
    size_t validPacketCount = 0;

    for (size_t i = 0; i + TS_PACKET_SIZE <= data.size(); i += TS_PACKET_SIZE) {
        if (isCancellationRequested(cancellationRequested)) {
            qInfo() << "TS文件处理过程中收到取消请求:" << filename;
            return false;
        }

        if (!validatePacket(data, i)) {
            invalidCount++;
            continue; // 跳过无效包
        }

        validPacketCount++;

        uint16_t pid = (data[i + 1] & 0x1F) << 8 | data[i + 2];

        if (isFirstFile) {
            if (pid == 0 && !pmtIdentified) {
                identifyPMTPID(data, i);
            }
            if (outfile.write(reinterpret_cast<const char*>(&data[i]), TS_PACKET_SIZE) != TS_PACKET_SIZE) {
                qCritical() << "写入输出文件失败:" << outfile.fileName();
                return false;
            }
            invokeTsMergerTestPacketProcessedHook();
            packetCount++;
        }
        else {
            if (pid == 0 || (pmtIdentified && pid == pmtPid)) {
                skippedCount++;
                continue; // 跳过PAT和PMT包
            }
            if (outfile.write(reinterpret_cast<const char*>(&data[i]), TS_PACKET_SIZE) != TS_PACKET_SIZE) {
                qCritical() << "写入输出文件失败:" << outfile.fileName();
                return false;
            }
            invokeTsMergerTestPacketProcessedHook();
            packetCount++;
        }
    }

    if (invalidCount > 0) {
        qWarning() << "跳过" << invalidCount << "个无效TS包";
    }
    if (skippedCount > 0) {
        qDebug() << "跳过" << skippedCount << "个PAT/PMT包";
    }

    if (validPacketCount == 0) {
        qCritical() << "文件未产生任何有效TS包，无法继续合并:" << filename << "文件大小:" << fileSize << "无效包数:" << invalidCount;
        return false;
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
