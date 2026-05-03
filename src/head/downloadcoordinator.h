#pragma once

#include <QObject>
#include <QList>
#include <QString>

#ifdef CORE_REGRESSION_TESTS
#include <functional>
#endif

#include "downloadcoordinatorseams.h"

class APIService;
class DownloadEngine;

#ifdef CORE_REGRESSION_TESTS
class QNetworkReply;
class QNetworkRequest;
class DownloadCoordinatorTestAdapter;
struct DecryptProcessRequest;
struct DecryptProcessResult;
struct FfmpegCliProcessRequest;
struct FfmpegCliProcessResult;
#endif

Q_DECLARE_METATYPE(DownloadJob)

class DownloadCoordinator : public QObject
{
    Q_OBJECT

#ifdef CORE_REGRESSION_TESTS
    friend class DownloadCoordinatorTestAdapter;
#endif

public:
    DownloadCoordinator(CoordinatorResolveService* resolveService,
        CoordinatorDownloadStage* downloadStage,
        CoordinatorConcatStage* concatStage,
        CoordinatorDecryptStage* decryptStage,
        CoordinatorDirectFinalizeStage* directFinalizeStage,
        QObject* parent = nullptr);
    DownloadCoordinator(APIService* apiService,
        CoordinatorDownloadStage* downloadStage,
        CoordinatorConcatStage* concatStage,
        CoordinatorDecryptStage* decryptStage,
        CoordinatorDirectFinalizeStage* directFinalizeStage,
        QObject* parent = nullptr);
    DownloadCoordinator(APIService* apiService,
        CoordinatorConcatStage* concatStage,
        CoordinatorDecryptStage* decryptStage,
        CoordinatorDirectFinalizeStage* directFinalizeStage,
        QObject* parent = nullptr);

    bool startSingle(const DownloadJob& job);
    bool startBatch(const QList<DownloadJob>& jobs);
    void cancelCurrent();
    void cancelAll();

    bool isBusy() const;
    int totalJobs() const;
    int completedJobs() const;
    int failedJobs() const;
    int cancelledJobs() const;
    QList<DownloadJob> jobs() const;

signals:
    void busyChanged(bool busy);
    void batchStarted(int totalJobs);
    void batchBusy();
    void jobChanged(const DownloadJob& job);
    void jobFinished(const DownloadJob& job);
    void shardInfoChanged(const DownloadInfo& info);
    void batchProgress(int completedJobs, int failedJobs, int cancelledJobs, int totalJobs);
    void fatalBatchFailure(const DownloadJob& job, DownloadErrorCategory category, const QString& message);
    void batchFinished(int completedJobs, int failedJobs, int cancelledJobs, int totalJobs, bool stoppedByFatalError);

private slots:
    void onResolved(const QStringList& segmentUrls, bool is4K);
    void onResolveFailed(DownloadErrorCategory category, const QString& message);
    void onResolveCancelled();
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal, const QVariant& userData);
    void onShardInfoChanged(const DownloadInfo& info, const QVariant& userData);
    void onDownloadFinished(bool success, const QString& errorString, const QVariant& userData);
    void onConcatFinished(bool ok, const QString& message);
    void onDecryptFinished(bool ok, const QString& message);
    void onDirectFinalizeFinished(bool ok, const QString& code, const QString& message, const QString& finalPath);

private:
    void resetBatchState();
    void startNextQueuedJob();
    void scheduleNextQueuedJob();
    void finishBatch();
    void emitBatchProgress();
    void dispatchCancelForCurrent();
    void markRemainingJobsCancelled(const QString& message);
    bool hasCurrentJob() const;
    bool isTerminalState(DownloadJobState state) const;
    bool transitionJob(int index, DownloadJobState newState, DownloadJobStage newStage);
    void emitCurrentJobChanged();
    void completeCurrentJob();
    void failCurrentJob(DownloadErrorCategory category, const QString& message);
    void cancelCurrentJob(const QString& message = QStringLiteral("cancelled"));
    DownloadErrorCategory classifyConcatFailure(const QString& message) const;
    DownloadErrorCategory classifyDecryptFailure(const QString& message) const;
    DownloadErrorCategory classifyDirectFinalizeFailure(const QString& code, const QString& message) const;
    QString taskDirectoryForJob(const DownloadJob& job) const;
    QString currentJobTaskDirectory() const;
    QString currentJobId() const;

#ifdef CORE_REGRESSION_TESTS
    void setTestDownloadReplyFactory(const std::function<QNetworkReply*(const QNetworkRequest&)>& replyFactory);
    void clearTestDownloadReplyFactory();
    void setTestDownloadPolicies(int initialTimeoutMs,
        int initialMaxAttempts,
        int initialRetryDelayMs,
        int recoveryTimeoutMs,
        int recoveryMaxAttempts,
        int recoveryRetryDelayMs);
    void setTestDecryptProcessRunner(const std::function<DecryptProcessResult(const DecryptProcessRequest&)>& runner);
    void clearTestDecryptProcessRunner();
    void setTestDecryptAssetsDir(const QString& decryptAssetsDir);
    void clearTestDecryptAssetsDir();
    void setTestDecryptStageShutdownWaitMs(int waitMs);
    void setTestDecryptStageLifecycleObserver(const std::function<void(const QString&)>& observer);
    void setTestDirectFinalizeProcessRunner(const std::function<FfmpegCliProcessResult(const FfmpegCliProcessRequest&)>& runner);
    void clearTestDirectFinalizeProcessRunner();
    void setTestDirectFinalizeAssetsDir(const QString& decryptAssetsDir);
    void clearTestDirectFinalizeAssetsDir();
#endif

    CoordinatorResolveService* m_resolveService = nullptr;
    CoordinatorResolveService* m_ownedResolveService = nullptr;
    CoordinatorDownloadStage* m_downloadStage = nullptr;
    CoordinatorDownloadStage* m_ownedDownloadStage = nullptr;
    DownloadEngine* m_ownedDownloadEngine = nullptr;
    CoordinatorConcatStage* m_concatStage = nullptr;
    CoordinatorDecryptStage* m_decryptStage = nullptr;
    CoordinatorDirectFinalizeStage* m_directFinalizeStage = nullptr;

    QList<DownloadJob> m_jobs;
    int m_currentIndex = -1;
    int m_completedJobs = 0;
    int m_failedJobs = 0;
    int m_cancelledJobs = 0;
    int m_nextGeneratedId = 1;
    bool m_busy = false;
    bool m_cancelCurrentRequested = false;
    bool m_cancelAllRequested = false;
    bool m_stoppedByFatalError = false;
    bool m_currentJobIs4K = false;
};
