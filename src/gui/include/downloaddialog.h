#pragma once

#include <QtWidgets/QDialog>
#include <QCryptographicHash>
#include <QStandardItemModel>
#include <QDir>
#include <QSet>
#include <QList>
#include "ui_dialog.h"
#include "downloadengine.h"
#include "downloadmodel.h"

#ifdef CORE_REGRESSION_TESTS
#include <functional>
#endif

#ifdef CORE_REGRESSION_TESTS
class QNetworkReply;
class QNetworkRequest;
class DownloadDialogTestAdapter;
#endif

class QResizeEvent;

class Download : public QDialog
{
	Q_OBJECT

#ifdef CORE_REGRESSION_TESTS
	friend class DownloadDialogTestAdapter;
#endif

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
	void DownloadFinished(bool success);

private slots:
	void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal, const QVariant& userData);
	void onDownloadFinished(bool success, const QString& errorString, const QVariant& userData);
	void onAllDownloadFinished();

protected:
	void closeEvent(QCloseEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

	//void updateProgress();

private:
	enum class DownloadPhase {
		Initial,
		Recovery
	};

	void applyInitialDownloadPolicy();
	void startRecoveryDownloads();
	QString originalUrlForIndex(int index) const;
	QString shardStateFilePath() const;
	QString shardFilePathForIndex(int index) const;
	QList<int> pendingIndexesFromState() const;
	void restorePersistedShardState();
	void persistShardState() const;
	void clearPersistedShardState() const;
	void scheduleCompletionSignal(bool success);
	bool hasPersistedShardState() const;
	void layoutDownloadDialog();

#ifdef CORE_REGRESSION_TESTS
	void setTestReplyFactory(const std::function<QNetworkReply*(const QNetworkRequest&)>& replyFactory);
	void clearTestReplyFactory();
	void setTestDownloadPolicies(int initialTimeoutMs,
		int initialMaxAttempts,
		int initialRetryDelayMs,
		int recoveryTimeoutMs,
		int recoveryMaxAttempts,
		int recoveryRetryDelayMs);
#endif

	Ui::Dialog ui;
	QString m_savePath;
	QStringList m_urls;
	DownloadEngine* m_engine;
	DownloadModel* m_model;
	QHash<int, DownloadInfo> m_infoList;
	bool m_downloadSuccessful = true;
	bool m_userCancelled = false;
	DownloadPhase m_phase = DownloadPhase::Initial;
	int m_initialThreadCount = 1;
	QSet<int> m_failedIndexes;
	int m_initialTimeoutMs = 45000;
	int m_initialMaxAttempts = 3;
	int m_initialRetryDelayMs = 1500;
	int m_recoveryTimeoutMs = 90000;
	int m_recoveryMaxAttempts = 5;
	int m_recoveryRetryDelayMs = 4000;
	bool m_completionSignalScheduled = false;
};
