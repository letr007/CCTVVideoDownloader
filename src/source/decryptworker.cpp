#include "../head/decryptworker.h"
#include <QCryptographicHash>
#include <QDir>
#include <QProcess>
#include <QStringConverter>
#include <QCoreApplication>
#include <QRegularExpression>

bool removeDirectory(const QString& path) {
    qDebug() << "删除目录:" << path;
    
	QDir dir(path);
	if (!dir.exists()) {
        qDebug() << "目录不存在，无需删除";
		return true;
    }

	// 递归删除所有文件和子文件夹
	dir.setFilter(QDir::NoDotAndDotDot | QDir::AllEntries);
    QFileInfoList entries = dir.entryInfoList();
    qDebug() << "目录包含" << entries.size() << "个条目";
    
	for (const QFileInfo& info : entries) {
		if (info.isDir()) {
            qDebug() << "删除子目录:" << info.absoluteFilePath();
			if (!removeDirectory(info.absoluteFilePath()))
				return false;
		}
		else {
            qDebug() << "删除文件:" << info.absoluteFilePath();
			if (!QFile::remove(info.absoluteFilePath())) {
				qWarning() << "删除文件失败:" << info.absoluteFilePath();
				return false;
			}
		}
	}

	// 删除空文件夹本身
    qDebug() << "删除空目录:" << path;
	bool result = dir.rmdir(path);
    if (!result) {
        qWarning() << "删除目录失败:" << path;
    } else {
        qDebug() << "目录删除成功:" << path;
    }
    
	return result;
}

DecryptWorker::DecryptWorker(QObject* parent)
{
}

void DecryptWorker::doDecrypt()
{
    qInfo() << "开始视频解密 - 视频名称:" << m_name << "保存路径:" << m_savePath;
    
	auto nameHash = QString(
		QCryptographicHash::hash(m_name.toUtf8(), QCryptographicHash::Sha256)
		.toHex()
	);
	auto filePath = QDir::cleanPath(m_savePath + "/" + nameHash);
    qInfo() << "临时文件路径:" << filePath;

	QString mp4Path = QDir(filePath).filePath("result.mp4");
	QString cboxPath = QDir(filePath).filePath("input.cbox");
    qInfo() << "MP4文件路径:" << mp4Path << "CBOX文件路径:" << cboxPath;

	if (QFile::exists(cboxPath)) {
        qInfo() << "删除已存在的CBOX文件";
		QFile::remove(cboxPath);
    }

    qInfo() << "重命名MP4文件为CBOX文件";
	if (!QFile::rename(mp4Path, cboxPath)) {
        qWarning() << "重命名失败，尝试复制方式";
		if (!QFile::copy(mp4Path, cboxPath) || !QFile::remove(mp4Path)) {
			qCritical() << "重命名/复制 result.mp4 -> input.cbox 失败";
			emit decryptFinished(false, "重命名MP4->CBOX失败");
			return;
		}
        qInfo() << "通过复制方式完成文件转换";
	} else {
        qInfo() << "重命名成功";
    }
	
	QString outputFilePath = QDir(m_savePath).filePath("result.mp4");
	QString cboxExe = QDir(QCoreApplication::applicationDirPath()).filePath("decrypt/cbox.exe");
    qInfo() << "输出文件路径:" << outputFilePath << "CBOX执行文件:" << cboxExe;

	QProcess cbox;
	QStringList args;
	args << cboxPath << outputFilePath;
	cbox.setWorkingDirectory(m_savePath);
    qInfo() << "启动CBOX解密进程，参数:" << args;
	cbox.start(cboxExe, args);

	if (!cbox.waitForStarted())
	{
		qCritical() << "无法启动cbox进程";
		emit decryptFinished(false, "无法启动cbox");
		return;
	}

    qInfo() << "CBOX进程已启动，等待解密完成...";
	cbox.waitForFinished(-1);

	if (cbox.exitCode() != 0)
	{
		QString err = cbox.readAllStandardError();
		qCritical() << "CBOX解密失败，退出码:" << cbox.exitCode() << "错误信息:" << err;
		emit decryptFinished(false, "解密执行失败: " + err);
		return;
	}

    qInfo() << "CBOX解密成功完成，退出码:" << cbox.exitCode();
    
	// 清理视频名称中可能的路径非法字符
	m_name.replace(QRegularExpression(R"([\\/:*?"<>|])"), "_");
	qInfo() << "清理后的视频名称:" << m_name;

	QString finalPath = QDir(m_savePath).filePath("%1.mp4").arg(m_name);
	qInfo() << "初始文件路径:" << finalPath;

	// 生成不重复的最终文件路径
	int counter = 1;
	QString uniqueFinalPath = finalPath;
	QFileInfo fileInfo(finalPath);
	QString baseName = fileInfo.completeBaseName();
	QString suffix = fileInfo.suffix();
	QString path = fileInfo.path();

	while (QFile::exists(uniqueFinalPath)) {
		uniqueFinalPath = QDir(path).filePath(
			QString("%1(%2).%3").arg(baseName).arg(counter).arg(suffix)
		);
		counter++;

		// 防止无限循环
		if (counter > 1000) {
			qCritical() << "无法生成唯一最终文件路径，达到尝试次数上限";
			emit decryptFinished(false, "无法生成唯一的视频文件名");
			return;
		}
	}

	// 如果生成了新文件名，记录信息
	if (uniqueFinalPath != finalPath) {
		qInfo() << "原文件已存在，使用新文件名:" << uniqueFinalPath;
		finalPath = uniqueFinalPath;
	}

	qInfo() << "最终文件路径:" << finalPath;

	if (!QFile::rename(outputFilePath, finalPath))
	{
		qCritical() << "重命名视频文件失败，从" << outputFilePath << "到" << finalPath;
		emit decryptFinished(false, "重命名视频文件失败");
		return;
	}

	qInfo() << "重命名视频文件成功";

	if (!removeDirectory(filePath))
	{
		qWarning() << "移除临时文件夹失败，请手动清理:" << filePath;
		emit decryptFinished(false, "移除临时文件夹失败\n请手动清理");
	} else {
        qInfo() << "临时文件夹清理成功";
    }

    qInfo() << "清理临时文件";
	QFile::remove(QDir(m_savePath).filePath("output.txt"));
	QFile::remove(QDir(m_savePath).filePath("UDRM_LICENSE.v1.0"));

    qInfo() << "视频解密全部完成，输出文件:" << finalPath;
	emit decryptFinished(true, "解密完成，输出 " + m_name);
}