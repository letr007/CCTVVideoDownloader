// concatworker.cpp
#include "../head/concatworker.h"
#include "../head/tsmerger.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <algorithm>
#include <QCoreApplication>
#include <QProcess>

ConcatWorker::ConcatWorker(QObject* parent) : QObject(parent)
{
}

void ConcatWorker::cancelConcat()
{
    m_cancelled.store(true, std::memory_order_relaxed);
}

void ConcatWorker::doConcat()
{
    qInfo() << "开始视频拼接，文件路径:" << m_filePath;

    if (m_cancelled.load(std::memory_order_relaxed)) {
        emit concatFinished(false, QStringLiteral("cancelled"));
        return;
    }
    
    QDir fileDir(m_filePath);
    if (!fileDir.exists()) {
        qCritical() << "拼接失败: 目录不存在 -" << m_filePath;
        emit concatFinished(false, "目录不存在");
        return;
    }

    fileDir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
    fileDir.setNameFilters(QStringList() << "*.ts");

    QFileInfoList fileList = fileDir.entryInfoList();
    if (fileList.isEmpty()) {
        qWarning() << "拼接失败: 未找到 ts 文件 -" << m_filePath;
        emit concatFinished(false, "未找到 ts 文件");
        return;
    }

    qInfo() << "找到" << fileList.size() << "个TS文件";

    std::sort(fileList.begin(), fileList.end(),
        [](const QFileInfo& a, const QFileInfo& b) {
            return a.baseName().toInt() < b.baseName().toInt();
        });

	std::vector<QString> tsFilePaths;
    for (const QFileInfo& fi : fileList) {
        if (fi.size() == 0) {
            qCritical() << "拼接失败: 存在空文件 -" << fi.absoluteFilePath();
            emit concatFinished(false, QString("存在空文件: %1").arg(fi.fileName()));
            return;
        }
        tsFilePaths.push_back(fi.absoluteFilePath());
        qDebug() << "TS文件:" << fi.fileName() << "大小:" << fi.size() << "字节";
    }

    QString outPath = QDir(m_filePath).filePath("result.ts");
    qInfo() << "TS暂存输出文件路径:" << outPath;

	TSMerger merger;
	merger.reset();
    qInfo() << "开始合并TS文件...";

    if (merger.merge(tsFilePaths, outPath, [this]() {
        return m_cancelled.load(std::memory_order_relaxed);
    })) {
        if (m_cancelled.load(std::memory_order_relaxed)) {
            if (QFile::exists(outPath) && !QFile::remove(outPath)) {
                qWarning() << "取消后清理 result.ts 失败:" << outPath;
            }
            emit concatFinished(false, QStringLiteral("cancelled"));
            return;
        }

        qInfo() << "视频拼接成功完成，TS暂存文件:" << outPath;
        emit concatFinished(true, "TS暂存完成，输出 result.ts");
    }
    else {
        if (m_cancelled.load(std::memory_order_relaxed)) {
            emit concatFinished(false, QStringLiteral("cancelled"));
            return;
        }

        qCritical() << "视频拼接失败";
        emit concatFinished(false, "拼接失败");
    }
}
