#include "decryptworker.h"
#include <QCryptographicHash>
#include <QDir>
#include <QProcess>
#include <QStringConverter>

bool removeDirectory(const QString& path) {
	QDir dir(path);
	if (!dir.exists())
		return true;

	// 递归删除所有文件和子文件夹
	dir.setFilter(QDir::NoDotAndDotDot | QDir::AllEntries);
	for (const QFileInfo& info : dir.entryInfoList()) {
		if (info.isDir()) {
			if (!removeDirectory(info.absoluteFilePath()))
				return false;
		}
		else {
			if (!QFile::remove(info.absoluteFilePath())) {
				qWarning() << "Failed to remove file:" << info.absoluteFilePath();
				return false;
			}
		}
	}

	// 删除空文件夹本身
	return dir.rmdir(path);
}

DecryptWorker::DecryptWorker(QObject* parent)
{
}

void DecryptWorker::doDecrypt()
{
	auto nameHash = QString(
		QCryptographicHash::hash(m_name.toUtf8(), QCryptographicHash::Sha256)
		.toHex()
	);
	auto filePath = QDir::cleanPath(m_savePath + "/" + nameHash);

	QString mp4Path = QDir(filePath).filePath("result.mp4");
	QString cboxPath = QDir(filePath).filePath("input.cbox");

	if (QFile::exists(cboxPath))
		QFile::remove(cboxPath);

	if (!QFile::rename(mp4Path, cboxPath)) {
		if (!QFile::copy(mp4Path, cboxPath) || !QFile::remove(mp4Path)) {
			qWarning() << "重命名/复制 result.mp4 -> input.cbox 失败";
			emit decryptFinished(false, "重命名MP4->CBOX失败");
			return;
		}
	}
	
	QString outputFilePath = QDir(m_savePath).filePath("result.mp4");
	QString cboxExe = QDir(QDir::currentPath()).filePath("decrypt/cbox.exe");

	QProcess cbox;
	QStringList args;
	args << cboxPath << outputFilePath;
	cbox.setWorkingDirectory(QDir::currentPath());
	cbox.start(cboxExe, args);

	if (!cbox.waitForStarted())
	{
		emit decryptFinished(false, "无法启动cbox");
		return;
	}

	cbox.waitForFinished(-1);

	if (cbox.exitCode() != 0)
	{
		QString err = cbox.readAllStandardError();
		emit decryptFinished(false, "解密执行失败: " + err);
		return;
	}

	if (!QFile::rename(outputFilePath, QDir(m_savePath).filePath("%1.mp4").arg(m_name)))
	{
		emit decryptFinished(false, "重命名视频文件失败");
	}

	if (!removeDirectory(filePath))
	{
		emit decryptFinished(false, "移除临时文件夹失败\n请手动清理");
	}

	emit decryptFinished(true, "解密完成，输出 " + m_name);
}