// concatworker.cpp
#include "../head/concatworker.h"
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

void ConcatWorker::doConcat()
{
    QDir fileDir(m_filePath);
    if (!fileDir.exists()) {
        emit concatFinished(false, "目录不存在");
        return;
    }

    fileDir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
    fileDir.setNameFilters(QStringList() << "*.ts");

    QFileInfoList fileList = fileDir.entryInfoList();
    if (fileList.isEmpty()) {
        emit concatFinished(false, "未找到 ts 文件");
        return;
    }

    std::sort(fileList.begin(), fileList.end(),
        [](const QFileInfo& a, const QFileInfo& b) {
            return a.baseName().toInt() < b.baseName().toInt();
        });

    QFile concatFile(QDir(QDir(m_filePath).absolutePath()).filePath("filelist.txt"));
    if (!concatFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit concatFinished(false, "创建 filelist.txt 失败");
        return;
    }

    QTextStream out(&concatFile);
    for (const QFileInfo& fi : fileList)
        out << "file '" << fi.fileName() << "'\n";

    concatFile.close();
    
    QString ffmpegExe = QDir(QDir::currentPath()).filePath("ffmpeg/ffmpeg.exe");
    QString txtPath = QDir(m_filePath).filePath("filelist.txt");
    QString outPath = QDir(m_filePath).filePath("result.mp4");

    QStringList args;
    args << "-f" << "concat"
        << "-safe" << "0"
        << "-i" << txtPath
        << "-codec" << "copy"
        << "-y" << outPath;

    QProcess ffmpeg;
    ffmpeg.setWorkingDirectory(m_filePath);
    ffmpeg.start(ffmpegExe, args);

    if (!ffmpeg.waitForStarted()) {
        emit concatFinished(false, "无法启动 ffmpeg");
        return;
    }

    ffmpeg.waitForFinished(-1);

    if (ffmpeg.exitCode() != 0) {
        QString err = ffmpeg.readAllStandardError();
        emit concatFinished(false, "ffmpeg执行失败: " + err);
        return;
    }

    emit concatFinished(true, "拼接完成，输出 result.mp4");
}