#pragma once

#include <QtWidgets/QDialog>
#include <QJsonObject>
#include <QJsonDocument>
#include "ui_import.h"
#include "config.h"

class Import : public QDialog
{
	Q_OBJECT

public:
	Import(QWidget* parent);
	~Import();
	void ImportProgrammeFromUrl();

private:
	Ui::ImportDialog ui;

signals:
	void importFinished();
};
