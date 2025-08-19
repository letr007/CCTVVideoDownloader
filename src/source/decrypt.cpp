#include "../head/decrypt.h"
#include <QThread>
#include <QCryptographicHash>
#include <QDir>
#include <QMessageBox>

Decrypt::Decrypt(QWidget* parent)
{
	ui.setupUi(this);
	ui.label_text->setText("正在进行视频解密...");
	worker = new DecryptWorker();
	decryptThread = new QThread();
}

Decrypt::~Decrypt()
{
}

void Decrypt::transferDecryptParams(const QString& name, const QString& savePath)
{
	worker->setParams(name, savePath);
	worker->moveToThread(decryptThread);
	connect(decryptThread, &QThread::started, worker, &DecryptWorker::doDecrypt);
	connect(worker, &DecryptWorker::decryptFinished, this, &Decrypt::onDecryptFinished);
	connect(decryptThread, &QThread::finished, worker, &QObject::deleteLater);
	decryptThread->start();
}

void Decrypt::onDecryptFinished(bool ok, const QString& msg)
{
	if (ok)
	{
		//qDebug() << msg;
		emit DecryptFinished();
		close();
	}
	else
	{
		qWarning() << msg;
		QMessageBox::warning(NULL, "发生错误", msg, QMessageBox::Yes, QMessageBox::Yes);
		close();
	}
}