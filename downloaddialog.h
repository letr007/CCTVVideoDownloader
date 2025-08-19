#pragma once

#include <QtWidgets/QDialog>
#include <QCryptographicHash>
#include <QStandardItemModel>
#include <QDir>
#include "ui_dialog.h"
#include "downloadengine.h"
#include "downloadmodel.h"

class Download : public QDialog
{
	Q_OBJECT

public:
	Download(QWidget* parent);
	~Download();

	void transferDwonloadParams(
		const QString& name,
		const QStringList& urls,
		const QString& savePath,
		const int& threadNum
	);

	void stratDownload();

signals:
	void DownloadFinished();

private slots:
	void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal, const QVariant& userData);
	void onDownloadFinished(bool success, const QString& errorString, const QVariant& userData);
	void onAllDownloadFinished();

protected:
	void closeEvent(QCloseEvent* event) override;

	//void updateProgress();

private:
	Ui::Dialog ui;
	QString m_savePath;
	QStringList m_urls;
	DownloadEngine* m_engine;
	DownloadModel* m_model;
	QHash<int, DownloadInfo> m_infoList;
};
