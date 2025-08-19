#include "../head/concat.h"
#include <QThread>
#include <QCryptographicHash>
#include <QDir>
#include <QMessageBox>

Concat::Concat(QWidget* parent)
{
	ui.setupUi(this);
	ui.label_text->setText("正在进行视频拼接...");
	worker = new ConcatWorker();
	concatThread = new QThread();
}

Concat::~Concat()
{
}

void Concat::transferConcatParams(const QString& name, const QString& savePath)
{
	auto nameHash = QString(
		QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256)
		.toHex()
	);
	auto filePath = QDir::cleanPath(savePath + "/" + nameHash);

	worker->setFilePath(filePath);
	worker->moveToThread(concatThread);
	connect(concatThread, &QThread::started, worker, &ConcatWorker::doConcat);
	connect(worker, &ConcatWorker::concatFinished, this, &Concat::onConcatFinished);
	connect(concatThread, &QThread::finished, worker, &QObject::deleteLater);
	concatThread->start();
}

void Concat::onConcatFinished(bool ok, const QString& msg)
{
	if (ok)
	{
		//qDebug() << msg;
		emit ConcatFinished();
		close();
	}
	else
	{
		qWarning() << msg;
		QMessageBox::warning(NULL, "发生错误", msg, QMessageBox::Yes, QMessageBox::Yes);
		close();
	}
}