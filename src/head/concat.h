#pragma once

#include "ui_process.h"
#include "concatworker.h"
#include <QtWidgets/QDialog>

class Concat : public QDialog
{
	Q_OBJECT

public:
	Concat(QWidget *parent);
	~Concat();
	void transferConcatParams(const QString& name,  const QString& savePath);

signals:
	void ConcatFinished();

private slots:
	void onConcatFinished(bool ok, const QString& msg);

private:
	Ui::Process ui;
	ConcatWorker* worker;
	QThread* concatThread;
};