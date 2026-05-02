#pragma once

#include <QObject>
#include <QString>

struct DirectMediaFinalizeResult
{
	bool ok = false;
	QString code;
	QString message;
	QString finalPath;
};

DirectMediaFinalizeResult finalizeDirectTsTask(const QString& title,
	const QString& savePath,
	bool transcodeToMp4);

class DirectFinalizeWorker : public QObject
{
	Q_OBJECT

public:
	explicit DirectFinalizeWorker(QObject* parent = nullptr) : QObject(parent) {}

public slots:
	void doWork(const QString& title, const QString& savePath, bool transcodeToMp4);

signals:
	void finished(bool ok, const QString& code, const QString& message, const QString& finalPath);
};
