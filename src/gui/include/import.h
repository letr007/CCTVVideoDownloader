#pragma once

#include <QtWidgets/QDialog>
#include <QJsonObject>
#include <QJsonDocument>
#include "ui_import.h"
#include "config.h"

enum class ImportMode {
	DownloadPageVideo,
	ListCatalogue,
	Cancelled
};

ImportMode askImportMode(QWidget* parent, const ContentParse::ImportResult& result);
void setTestImportMode(ImportMode mode);
void clearTestImportMode();

class Import : public QDialog
{
	Q_OBJECT

public:
	Import(QWidget* parent);
	~Import();
	void ImportProgrammeFromUrl();
	void handlePlayColumnInfoResolved(quint64 requestId, const ContentParse::ImportResult& data);
	void handlePlayColumnInfoFailed(quint64 requestId, const QString& errorMessage);

private:
	void setBusy(bool busy);
	quint64 m_pendingPlayColumnInfoRequestId = 0;
	Ui::ImportDialog ui;

signals:
	void importFinished();
	void pageVideoDownloadRequested(const QString& guid, const QString& title);
};
