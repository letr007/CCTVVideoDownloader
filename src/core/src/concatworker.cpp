// concatworker.cpp
#include "../../include/concatworker.h"
#include "../../include/tsmerger.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

ConcatWorker::ConcatWorker(QObject* parent) : QObject(parent)
{
}

void ConcatWorker::cancelConcat()
{
    m_cancelled.store(true, std::memory_order_relaxed);
}

void ConcatWorker::doConcat()
{
    qInfo() << "开始视频拼接，输出路径:" << m_outputPath;

    if (m_cancelled.load(std::memory_order_relaxed)) {
        emit concatFinished(false, QStringLiteral("cancelled"));
        return;
    }

    if (m_inputPaths.isEmpty()) {
        emit concatFinished(false, QStringLiteral("未提供分片清单"));
        return;
    }
    if (m_outputPath.isEmpty()) {
        emit concatFinished(false, QStringLiteral("未提供输出路径"));
        return;
    }

    const QFileInfo outputInfo(m_outputPath);
    const QDir taskDir = outputInfo.absoluteDir();
    const QString canonicalTaskDir = taskDir.canonicalPath();
    if (canonicalTaskDir.isEmpty()) {
        qCritical() << "拼接失败: 任务目录不存在 -" << taskDir.absolutePath();
        emit concatFinished(false, QStringLiteral("任务目录不存在"));
        return;
    }

    const QString outputPath = QDir::cleanPath(outputInfo.absoluteFilePath());
    const QString canonicalOutputPath = outputInfo.exists()
        ? outputInfo.canonicalFilePath()
        : outputPath;
    std::vector<QString> tsFilePaths;
    tsFilePaths.reserve(m_inputPaths.size());
    for (const QString& inputPath : m_inputPaths) {
        const QFileInfo inputInfo(inputPath);
        if (!inputInfo.exists()) {
            qCritical() << "拼接失败: 分片文件不存在 -" << inputPath;
            emit concatFinished(false, QStringLiteral("分片文件不存在: %1").arg(inputPath));
            return;
        }
        if (!inputInfo.isFile() || inputInfo.isSymLink()) {
            qCritical() << "拼接失败: 分片不是普通文件 -" << inputPath;
            emit concatFinished(false, QStringLiteral("分片不是普通文件: %1").arg(inputPath));
            return;
        }
        if (inputInfo.size() <= 0) {
            qCritical() << "拼接失败: 存在空文件 -" << inputPath;
            emit concatFinished(false, QStringLiteral("存在空文件: %1").arg(inputInfo.fileName()));
            return;
        }

        const QString canonicalInputPath = inputInfo.canonicalFilePath();
        const QString relativeInputPath = QDir(canonicalTaskDir).relativeFilePath(canonicalInputPath);
        if (relativeInputPath == QStringLiteral("..") || relativeInputPath.startsWith(QStringLiteral("../"))) {
            qCritical() << "拼接失败: 分片不在任务目录内 -" << inputPath;
            emit concatFinished(false, QStringLiteral("分片不在任务目录内: %1").arg(inputPath));
            return;
        }
        if (QDir::cleanPath(inputInfo.absoluteFilePath()) == outputPath
            || canonicalInputPath == canonicalOutputPath) {
            qCritical() << "拼接失败: 分片不能是输出 result.ts -" << inputPath;
            emit concatFinished(false, QStringLiteral("分片不能是输出 result.ts"));
            return;
        }

        tsFilePaths.push_back(canonicalInputPath);
    }

    qInfo() << "开始合并" << tsFilePaths.size() << "个显式TS分片，输出:" << outputPath;
    TSMerger merger;
    merger.reset();

    if (merger.merge(tsFilePaths, outputPath, [this]() {
        return m_cancelled.load(std::memory_order_relaxed);
    })) {
        if (m_cancelled.load(std::memory_order_relaxed)) {
            if (QFile::exists(outputPath) && !QFile::remove(outputPath)) {
                qWarning() << "取消后清理 result.ts 失败:" << outputPath;
            }
            emit concatFinished(false, QStringLiteral("cancelled"));
            return;
        }

        qInfo() << "视频拼接成功完成，TS暂存文件:" << outputPath;
        emit concatFinished(true, QStringLiteral("TS暂存完成，输出 result.ts"));
        return;
    }

    if (m_cancelled.load(std::memory_order_relaxed)) {
        emit concatFinished(false, QStringLiteral("cancelled"));
        return;
    }

    qCritical() << "视频拼接失败";
    emit concatFinished(false, QStringLiteral("拼接失败"));
}
