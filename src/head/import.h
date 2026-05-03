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
	void handlePlayColumnInfoResolved(quint64 requestId, const QStringList& data);
	void handlePlayColumnInfoFailed(quint64 requestId, const QString& errorMessage);

private:
	void setBusy(bool busy);
	quint64 m_pendingPlayColumnInfoRequestId = 0;
	Ui::ImportDialog ui;

signals:
	void importFinished();
};
