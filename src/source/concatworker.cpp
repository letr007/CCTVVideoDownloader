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

	std::vector<QString> tsFilePaths;
    for (const QFileInfo& fi : fileList)
		tsFilePaths.push_back(fi.absoluteFilePath());

    QString outPath = QDir(m_filePath).filePath("result.mp4");

	TSMerger merger;
	merger.reset();
    if (merger.merge(tsFilePaths, outPath)) {
        emit concatFinished(true, "拼接完成，输出 result.mp4");
    }
    else {
        emit concatFinished(false, "拼接失败");
    }
}