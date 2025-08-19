#pragma once

#include "ui_process.h"
#include "decryptworker.h"
#include <QtWidgets/QDialog>

class Decrypt : public QDialog
{
	Q_OBJECT

public:
	Decrypt(QWidget* parent);
	~Decrypt();
	void transferDecryptParams(const QString& name, const QString& savePath);

signals:
	void DecryptFinished();

private slots:
	void onDecryptFinished(bool ok, const QString& msg);

private:
	Ui::Process ui;
	DecryptWorker* worker;
	QThread* decryptThread;
};