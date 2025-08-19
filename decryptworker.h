#pragma once

#include <QObject>

class DecryptWorker : public QObject
{
	Q_OBJECT
public:
	explicit DecryptWorker(QObject* parent = nullptr);

	void setParams(const QString& name, const QString& savePath) { m_name = name; m_savePath = savePath; }

public slots:
	void doDecrypt();

signals:
	void decryptFinished(bool ok, const QString& msg);

private:
	QString m_name;
	QString m_savePath;
};