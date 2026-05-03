#include "../head/downloadcoordinator.h"

#include "../head/apiservice.h"
#include "../head/concatworker.h"
#include "../head/decryptworker.h"
#include "../head/directmediafinalizer.h"
#include "../head/downloadengine.h"
#include "../head/downloadmodel.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <memory>

namespace {

QString hashedTaskDirectory(const QString& title, const QString& savePath, const QString& jobId)
{
    QString identity = title;
    if (!jobId.isEmpty()) {
        identity += QStringLiteral("\n");
        identity += jobId;
    }

    const QString nameHash = QString(
        QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex()
    );
    return QDir::cleanPath(savePath + "/" + nameHash);
}

class ProductionCoordinatorDownloadStage final : public CoordinatorDownloadStage
{
    Q_OBJECT

public:
    explicit ProductionCoordinatorDownloadStage(DownloadEngine* engine, QObject* parent = nullptr)
        : CoordinatorDownloadStage(parent)
        , m_engine(engine)
    {
        Q_ASSERT(m_engine != nullptr);

        connect(m_engine, &DownloadEngine::downloadProgress,
            this, &ProductionCoordinatorDownloadStage::onEngineDownloadProgress);
        connect(m_engine, &DownloadEngine::downloadFinished,
            this, &ProductionCoordinatorDownloadStage::onEngineDownloadFinished);
        connect(m_engine, &DownloadEngine::allDownloadFinished,
            this, &ProductionCoordinatorDownloadStage::onEngineAllDownloadFinished);
    }

    void startDownload(const QStringList& segmentUrls, const QString& saveDir, const QVariant& userData) override
    {
        m_jobUserData = userData;
        m_saveDir = saveDir;
        m_urls = segmentUrls;
        m_infoList.clear();
        m_progressByIndex.clear();
        m_segmentUserDataToIndex.clear();
        m_failedIndexes.clear();
        m_downloadSuccessful = true;
        m_userCancelled = false;
        m_completionSignalScheduled = false;
        m_lastFailure.clear();
        applyInitialDownloadPolicy();

        for (int index = 1; index <= m_urls.size(); ++index) {
            DownloadInfo info;
            info.index = index;
            info.status = DownloadStatus::Waiting;
            info.url = originalUrlForIndex(index);
            info.progress = 0;
            m_infoList.insert(index, info);
            m_progressByIndex.insert(index, 0);
        }

        restorePersistedShardState();
        persistShardState();
        emitShardInfoList();
        emitAggregateProgress();

        if (m_urls.isEmpty()) {
            m_downloadSuccessful = false;
            m_lastFailure = QStringLiteral("no segment urls");
            scheduleCompletionSignal(false, m_lastFailure);
            return;
        }

        if (pendingIndexesFromState().isEmpty()) {
            clearPersistedShardState();
            scheduleCompletionSignal(true, QString());
            return;
        }

        startPendingDownloads();
    }

    void setInitialThreadCount(int threadCount)
    {
        m_initialThreadCount = std::max(1, threadCount);
    }

    void cancelDownload(const QVariant& userData) override
    {
        if (!m_jobUserData.isValid() || userData != m_jobUserData) {
            return;
        }

        requestCancel();
    }

    void cancelAllDownloads() override
    {
        requestCancel();
    }

#ifdef CORE_REGRESSION_TESTS
    void setTestReplyFactory(const std::function<QNetworkReply*(const QNetworkRequest&)>& replyFactory)
    {
        m_engine->setTestReplyFactory(replyFactory);
    }

    void clearTestReplyFactory()
    {
        m_engine->clearTestReplyFactory();
    }

    void setDownloadPolicies(int initialTimeoutMs,
        int initialMaxAttempts,
        int initialRetryDelayMs,
        int recoveryTimeoutMs,
        int recoveryMaxAttempts,
        int recoveryRetryDelayMs)
    {
        m_initialTimeoutMs = initialTimeoutMs;
        m_initialMaxAttempts = initialMaxAttempts;
        m_initialRetryDelayMs = initialRetryDelayMs;
        m_recoveryTimeoutMs = recoveryTimeoutMs;
        m_recoveryMaxAttempts = recoveryMaxAttempts;
        m_recoveryRetryDelayMs = recoveryRetryDelayMs;
    }
#endif

private slots:
    void onEngineDownloadProgress(qint64 bytesReceived, qint64 bytesTotal, const QVariant& userData)
    {
        const int index = segmentIndexForUserData(userData);
        if (index <= 0 || !m_infoList.contains(index)) {
            return;
        }

        double progress = bytesTotal > 0
            ? 100.0 * static_cast<double>(bytesReceived) / static_cast<double>(bytesTotal)
            : 0.0;
        progress = qBound(0.0, progress, 100.0);

        DownloadInfo info = m_infoList.value(index);
        info.progress = static_cast<int>(progress);
        if (progress > 0.0) {
            info.status = DownloadStatus::Downloading;
        }
        m_infoList[index] = info;
        m_progressByIndex[index] = info.progress;
        emitShardInfo(info);
        emitAggregateProgress();
    }

    void onEngineDownloadFinished(bool success, const QString& errorString, const QVariant& userData)
    {
        const int index = segmentIndexForUserData(userData);
        if (index <= 0 || !m_infoList.contains(index)) {
            return;
        }

        if (!success) {
            DownloadInfo info = m_infoList.value(index);
            info.status = DownloadStatus::Error;
            info.progress = 0;
            m_infoList[index] = info;
            m_progressByIndex[index] = 0;
            m_failedIndexes.insert(index);
            m_lastFailure = errorString;
            emitShardInfo(info);
            if (m_phase == DownloadPhase::Recovery && errorString != QStringLiteral("cancelled")) {
                m_downloadSuccessful = false;
            }
            persistShardState();
            emitAggregateProgress();
            return;
        }

        m_failedIndexes.remove(index);
        DownloadInfo info = m_infoList.value(index);
        info.status = DownloadStatus::Finished;
        info.progress = 100;
        info.url = originalUrlForIndex(index);
        m_infoList[index] = info;
        m_progressByIndex[index] = 100;
        emitShardInfo(info);
        persistShardState();
        emitAggregateProgress();
    }

    void onEngineAllDownloadFinished()
    {
        if (!m_jobUserData.isValid()) {
            return;
        }

        if (m_userCancelled) {
            persistShardState();
            scheduleCompletionSignal(false, QStringLiteral("cancelled"));
            return;
        }

        if (m_phase == DownloadPhase::Initial && !m_failedIndexes.isEmpty()) {
            persistShardState();
            startRecoveryDownloads();
            return;
        }

        persistShardState();
        const bool success = m_downloadSuccessful && m_failedIndexes.isEmpty();
        if (success) {
            clearPersistedShardState();
        }
        scheduleCompletionSignal(success, success ? QString() : m_lastFailure);
    }

private:
    enum class DownloadPhase {
        Initial,
        Recovery
    };

    void applyInitialDownloadPolicy()
    {
        m_phase = DownloadPhase::Initial;
        m_engine->setMaxThreadCount(m_initialThreadCount);
        m_engine->setDefaultTimeoutMs(m_initialTimeoutMs);
        m_engine->setDefaultMaxAttempts(m_initialMaxAttempts);
        m_engine->setDefaultRetryDelayMs(m_initialRetryDelayMs);
    }

    void startPendingDownloads()
    {
        const QList<int> pendingIndexes = pendingIndexesFromState();
        for (int index : pendingIndexes) {
            const QString segmentUserData = makeSegmentUserData(index);
            m_segmentUserDataToIndex.insert(segmentUserData, index);
            m_engine->download(originalUrlForIndex(index), m_saveDir, segmentUserData);
        }
    }

    void startRecoveryDownloads()
    {
        QList<int> recoveryIndexes = m_failedIndexes.values();
        if (recoveryIndexes.isEmpty()) {
            return;
        }

        std::sort(recoveryIndexes.begin(), recoveryIndexes.end());
        m_failedIndexes.clear();
        m_phase = DownloadPhase::Recovery;

        m_engine->setMaxThreadCount(1);
        m_engine->setDefaultTimeoutMs(m_recoveryTimeoutMs);
        m_engine->setDefaultMaxAttempts(m_recoveryMaxAttempts);
        m_engine->setDefaultRetryDelayMs(m_recoveryRetryDelayMs);

        for (int index : recoveryIndexes) {
            if (!m_infoList.contains(index)) {
                continue;
            }

            DownloadInfo info = m_infoList.value(index);
            info.status = DownloadStatus::Waiting;
            info.progress = 0;
            m_infoList[index] = info;
            m_progressByIndex[index] = 0;
            emitShardInfo(info);

            const QString segmentUserData = makeSegmentUserData(index);
            m_segmentUserDataToIndex.insert(segmentUserData, index);
            m_engine->download(originalUrlForIndex(index), m_saveDir, segmentUserData);
        }

        persistShardState();
        emitAggregateProgress();
    }

    QString originalUrlForIndex(int index) const
    {
        const int urlIndex = index - 1;
        if (urlIndex < 0 || urlIndex >= m_urls.size()) {
            return QString();
        }

        return m_urls.at(urlIndex);
    }

    QString shardStateFilePath() const
    {
        return QDir(m_saveDir).filePath(QStringLiteral(".download_state.json"));
    }

    bool hasPersistedShardState() const
    {
        return QFileInfo::exists(shardStateFilePath());
    }

    QString shardFilePathForIndex(int index) const
    {
        const QString shardUrl = originalUrlForIndex(index);
        if (shardUrl.isEmpty()) {
            return QString();
        }

        QUrl url(shardUrl);
        const QString fileName = QFileInfo(url.path()).fileName();
        if (fileName.isEmpty()) {
            return QString();
        }

        return QDir(m_saveDir).filePath(fileName);
    }

    QList<int> pendingIndexesFromState() const
    {
        QList<int> pendingIndexes;
        for (auto it = m_infoList.cbegin(); it != m_infoList.cend(); ++it) {
            if (it.value().status != DownloadStatus::Finished) {
                pendingIndexes.append(it.key());
            }
        }

        std::sort(pendingIndexes.begin(), pendingIndexes.end());
        return pendingIndexes;
    }

    void restorePersistedShardState()
    {
        QSet<int> persistedPendingIndexes;
        QSet<int> persistedCompletedIndexes;
        bool persistedUrlsMatch = false;
        const bool stateExists = hasPersistedShardState();
        QFile stateFile(shardStateFilePath());
        if (stateFile.open(QIODevice::ReadOnly)) {
            const QJsonDocument document = QJsonDocument::fromJson(stateFile.readAll());
            const QJsonObject root = document.object();
            const QJsonArray urlsArray = root.value(QStringLiteral("urls")).toArray();
            if (urlsArray.size() == m_urls.size()) {
                persistedUrlsMatch = true;
                for (int i = 0; i < urlsArray.size(); ++i) {
                    if (urlsArray.at(i).toString() != m_urls.at(i)) {
                        persistedUrlsMatch = false;
                        break;
                    }
                }
            }
            const QJsonArray pendingArray = root.value(QStringLiteral("pending_indexes")).toArray();
            const QJsonArray completedArray = root.value(QStringLiteral("completed_indexes")).toArray();
            for (const QJsonValue& value : pendingArray) {
                persistedPendingIndexes.insert(value.toInt());
            }
            for (const QJsonValue& value : completedArray) {
                persistedCompletedIndexes.insert(value.toInt());
            }
        }

        for (auto it = m_infoList.begin(); it != m_infoList.end(); ++it) {
            const int index = it.key();
            DownloadInfo info = it.value();
            const QString filePath = shardFilePathForIndex(index);
            const QFileInfo fileInfo(filePath);
            const bool isPersistedComplete = stateExists
                && persistedUrlsMatch
                && persistedCompletedIndexes.contains(index)
                && !persistedPendingIndexes.contains(index)
                && fileInfo.exists()
                && fileInfo.isFile()
                && fileInfo.size() > 0;

            if (isPersistedComplete) {
                info.status = DownloadStatus::Finished;
                info.progress = 100;
            } else {
                info.status = DownloadStatus::Waiting;
                info.progress = 0;
            }

            info.url = originalUrlForIndex(index);
            it.value() = info;
            m_progressByIndex[index] = info.progress;
        }
    }

    void persistShardState() const
    {
        if (m_saveDir.isEmpty()) {
            return;
        }

        const QList<int> pendingIndexes = pendingIndexesFromState();
        if (pendingIndexes.isEmpty()) {
            clearPersistedShardState();
            return;
        }

        QDir().mkpath(m_saveDir);

        QJsonObject root;
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("phase"), m_phase == DownloadPhase::Recovery ? QStringLiteral("recovery") : QStringLiteral("initial"));

        QJsonArray urlsArray;
        for (const QString& url : m_urls) {
            urlsArray.append(url);
        }
        root.insert(QStringLiteral("urls"), urlsArray);

        QJsonArray pendingArray;
        for (int index : pendingIndexes) {
            pendingArray.append(index);
        }
        root.insert(QStringLiteral("pending_indexes"), pendingArray);

        QJsonArray completedArray;
        for (auto it = m_infoList.cbegin(); it != m_infoList.cend(); ++it) {
            if (it.value().status == DownloadStatus::Finished) {
                completedArray.append(it.key());
            }
        }
        root.insert(QStringLiteral("completed_indexes"), completedArray);

        QSaveFile stateFile(shardStateFilePath());
        if (!stateFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return;
        }

        stateFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        stateFile.commit();
    }

    void clearPersistedShardState() const
    {
        const QString stateFilePath = shardStateFilePath();
        if (QFileInfo::exists(stateFilePath)) {
            QFile::remove(stateFilePath);
        }
    }

    void requestCancel()
    {
        if (!m_jobUserData.isValid() || m_userCancelled || m_engine->activeDownloads() <= 0) {
            return;
        }

        m_userCancelled = true;
        m_downloadSuccessful = false;
        m_lastFailure = QStringLiteral("cancelled");
        persistShardState();
        m_engine->cancelAll();
    }

    void scheduleCompletionSignal(bool success, const QString& errorString)
    {
        if (m_completionSignalScheduled) {
            return;
        }

        m_completionSignalScheduled = true;
        QTimer::singleShot(0, this, [this, success, errorString]() {
            m_completionSignalScheduled = false;
            emit downloadFinished(success, errorString, m_jobUserData);
            emit allDownloadFinished();
            m_jobUserData = QVariant();
            m_segmentUserDataToIndex.clear();
        });
    }

    QString makeSegmentUserData(int index) const
    {
        return QStringLiteral("%1::segment-%2").arg(m_jobUserData.toString()).arg(index);
    }

    int segmentIndexForUserData(const QVariant& userData) const
    {
        return m_segmentUserDataToIndex.value(userData.toString(), -1);
    }

    void emitAggregateProgress()
    {
        if (!m_jobUserData.isValid() || m_progressByIndex.isEmpty()) {
            return;
        }

        qint64 received = 0;
        for (auto it = m_progressByIndex.cbegin(); it != m_progressByIndex.cend(); ++it) {
            received += qBound(0, it.value(), 100);
        }
        const qint64 total = static_cast<qint64>(m_progressByIndex.size()) * 100;
        emit downloadProgress(received, total, m_jobUserData);
    }

    void emitShardInfo(const DownloadInfo& info)
    {
        if (m_jobUserData.isValid()) {
            emit shardInfoChanged(info, m_jobUserData);
        }
    }

    void emitShardInfoList()
    {
        QList<int> indexes = m_infoList.keys();
        std::sort(indexes.begin(), indexes.end());
        for (int index : indexes) {
            emitShardInfo(m_infoList.value(index));
        }
    }

    DownloadEngine* m_engine = nullptr;
    QVariant m_jobUserData;
    QString m_saveDir;
    QStringList m_urls;
    QHash<int, DownloadInfo> m_infoList;
    QHash<int, int> m_progressByIndex;
    QHash<QString, int> m_segmentUserDataToIndex;
    QSet<int> m_failedIndexes;
    bool m_downloadSuccessful = true;
    bool m_userCancelled = false;
    DownloadPhase m_phase = DownloadPhase::Initial;
    int m_initialThreadCount = 2;
    int m_initialTimeoutMs = 45000;
    int m_initialMaxAttempts = 3;
    int m_initialRetryDelayMs = 1500;
    int m_recoveryTimeoutMs = 90000;
    int m_recoveryMaxAttempts = 5;
    int m_recoveryRetryDelayMs = 4000;
    bool m_completionSignalScheduled = false;
    QString m_lastFailure;
};

} // namespace

class ProductionCoordinatorConcatStage final : public CoordinatorConcatStage
{
public:
    explicit ProductionCoordinatorConcatStage(QObject* parent = nullptr)
        : CoordinatorConcatStage(parent)
    {
    }

    ~ProductionCoordinatorConcatStage() override
    {
        shutdownActiveStage();
    }

    void setFilePath(const QString& path) override
    {
        m_filePath = path;
    }

    void startConcat() override
    {
        if (m_thread != nullptr || m_worker != nullptr) {
            emit concatFinished(false, QStringLiteral("concat stage busy"));
            return;
        }

        m_resultPending = false;
        m_resultOk = false;
        m_resultMessage.clear();

        auto* worker = new ConcatWorker();
        auto* thread = new QThread();
        worker->setFilePath(m_filePath);
        worker->moveToThread(thread);

        m_worker = worker;
        m_thread = thread;

        connect(thread, &QThread::started, worker, &ConcatWorker::doConcat);
        connect(worker, &ConcatWorker::concatFinished, this,
            [this, thread](bool ok, const QString& message) {
                m_resultPending = true;
                m_resultOk = ok;
                m_resultMessage = message;
                thread->quit();
            });
        connect(thread, &QThread::finished, this,
            [this, thread, worker]() {
                const bool shouldEmit = m_thread == thread && m_resultPending;
                const bool resultOk = m_resultOk;
                const QString resultMessage = m_resultMessage;
                if (m_worker == worker) {
                    m_worker = nullptr;
                }
                if (m_thread == thread) {
                    m_thread = nullptr;
                }
                m_resultPending = false;
                m_resultOk = false;
                m_resultMessage.clear();
                if (shouldEmit) {
                    emit concatFinished(resultOk, resultMessage);
                }
            });
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);

        thread->start();
    }

    void cancelConcat() override
    {
        if (m_worker != nullptr) {
            m_worker->cancelConcat();
        }
    }

private:
    void shutdownActiveStage()
    {
        auto* worker = m_worker;
        auto* thread = m_thread;
        m_worker = nullptr;
        m_thread = nullptr;
        m_resultPending = false;
        m_resultOk = false;
        m_resultMessage.clear();

        if (worker != nullptr) {
            worker->cancelConcat();
        }
        if (thread != nullptr) {
            thread->quit();
            thread->wait(5000);
        }
    }

    QString m_filePath;
    ConcatWorker* m_worker = nullptr;
    QThread* m_thread = nullptr;
    bool m_resultPending = false;
    bool m_resultOk = false;
    QString m_resultMessage;
};

class ProductionCoordinatorDecryptStage final : public CoordinatorDecryptStage
{
    Q_OBJECT

public:
    explicit ProductionCoordinatorDecryptStage(QObject* parent = nullptr)
        : CoordinatorDecryptStage(parent)
    {
    }

    ~ProductionCoordinatorDecryptStage() override
    {
        shutdownActiveStage();
    }

    void setParams(const QString& name, const QString& savePath) override
    {
        m_name = name;
        m_savePath = savePath;
    }

    void setTranscodeToMp4(bool transcodeToMp4) override
    {
        m_transcodeToMp4 = transcodeToMp4;
    }

    void setTaskDirectory(const QString& taskDirectory)
    {
        m_taskDirectory = taskDirectory;
    }

    void startDecrypt() override
    {
        if (m_thread != nullptr || m_worker != nullptr) {
            emit decryptFinished(false, QStringLiteral("decrypt stage busy"));
            return;
        }

        m_resultPending = false;
        m_resultOk = false;
        m_resultMessage.clear();

        auto* worker = new DecryptWorker();
        auto* thread = new QThread();
        worker->setParams(m_name, m_savePath);
        worker->setTaskDirectory(m_taskDirectory);
        worker->setTranscodeToMp4(m_transcodeToMp4);
#ifdef CORE_REGRESSION_TESTS
        if (m_testProcessRunner) {
            worker->setTestProcessRunner(m_testProcessRunner);
        }
        if (!m_testDecryptAssetsDir.isEmpty()) {
            worker->setTestDecryptAssetsDir(m_testDecryptAssetsDir);
        }
#endif
        worker->moveToThread(thread);

        m_worker = worker;
        m_thread = thread;

#ifdef CORE_REGRESSION_TESTS
        const auto lifecycleObserver = m_testLifecycleObserver;
        connect(worker, &QObject::destroyed, thread, [lifecycleObserver]() {
            if (lifecycleObserver && *lifecycleObserver) {
                (*lifecycleObserver)(QStringLiteral("worker_destroyed"));
            }
        });
        connect(thread, &QThread::finished, thread, [lifecycleObserver]() {
            if (lifecycleObserver && *lifecycleObserver) {
                (*lifecycleObserver)(QStringLiteral("thread_finished"));
            }
        });
#endif

        connect(thread, &QThread::started, worker, &DecryptWorker::doDecrypt);
        connect(worker, &DecryptWorker::decryptFinished, this,
            [this, thread](bool ok, const QString& message) {
                m_resultPending = true;
                m_resultOk = ok;
                m_resultMessage = message;
                thread->quit();
            });
        connect(thread, &QThread::finished, this,
            [this, thread, worker]() {
                const bool shouldEmit = m_thread == thread && m_resultPending;
                const bool resultOk = m_resultOk;
                const QString resultMessage = m_resultMessage;
                if (m_worker == worker) {
                    m_worker = nullptr;
                }
                if (m_thread == thread) {
                    m_thread = nullptr;
                }
                m_resultPending = false;
                m_resultOk = false;
                m_resultMessage.clear();
                if (shouldEmit) {
                    emit decryptFinished(resultOk, resultMessage);
                }
            });
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);

        thread->start();
    }

    void cancelDecrypt() override
    {
        if (m_worker != nullptr) {
            m_worker->cancelDecrypt();
        }
    }

#ifdef CORE_REGRESSION_TESTS
    void setTestProcessRunner(const std::function<DecryptProcessResult(const DecryptProcessRequest&)>& runner)
    {
        m_testProcessRunner = runner;
    }

    void clearTestProcessRunner()
    {
        m_testProcessRunner = {};
    }

    void setTestDecryptAssetsDir(const QString& decryptAssetsDir)
    {
        m_testDecryptAssetsDir = decryptAssetsDir;
    }

    void clearTestDecryptAssetsDir()
    {
        m_testDecryptAssetsDir.clear();
    }

    void setShutdownWaitMs(int waitMs)
    {
        m_shutdownWaitMs = std::max(0, waitMs);
    }

    void setLifecycleObserver(const std::function<void(const QString&)>& observer)
    {
        m_testLifecycleObserver = std::make_shared<std::function<void(const QString&)>>(observer);
    }
#endif

private:
    void shutdownActiveStage()
    {
        auto* worker = m_worker;
        auto* thread = m_thread;
        m_worker = nullptr;
        m_thread = nullptr;
        m_resultPending = false;
        m_resultOk = false;
        m_resultMessage.clear();

        if (worker != nullptr) {
            worker->cancelDecrypt();
        }
        if (thread != nullptr) {
            thread->quit();
            thread->wait(m_shutdownWaitMs);
        }
    }

    QString m_name;
    QString m_savePath;
    QString m_taskDirectory;
    bool m_transcodeToMp4 = false;
    DecryptWorker* m_worker = nullptr;
    QThread* m_thread = nullptr;
    bool m_resultPending = false;
    bool m_resultOk = false;
    QString m_resultMessage;
#ifdef CORE_REGRESSION_TESTS
    std::function<DecryptProcessResult(const DecryptProcessRequest&)> m_testProcessRunner;
    QString m_testDecryptAssetsDir;
    int m_shutdownWaitMs = 5000;
    std::shared_ptr<std::function<void(const QString&)>> m_testLifecycleObserver;
#else
    static constexpr int m_shutdownWaitMs = 5000;
#endif
};

class ProductionCoordinatorDirectFinalizeStage final : public CoordinatorDirectFinalizeStage
{
    Q_OBJECT

public:
    explicit ProductionCoordinatorDirectFinalizeStage(QObject* parent = nullptr)
        : CoordinatorDirectFinalizeStage(parent)
    {
    }

    ~ProductionCoordinatorDirectFinalizeStage() override
    {
        shutdownActiveStage();
    }

    void startFinalize(const QString& title, const QString& savePath, bool transcodeToMp4) override
    {
        if (m_thread != nullptr || m_worker != nullptr) {
            emit finished(false, QStringLiteral("direct_finalize_busy"), QStringLiteral("direct finalize stage busy"), QString());
            return;
        }

        m_resultPending = false;
        m_resultOk = false;
        m_resultCode.clear();
        m_resultMessage.clear();
        m_resultFinalPath.clear();

        auto* worker = new DirectFinalizeWorker();
        auto* thread = new QThread();
        worker->setTaskDirectory(m_taskDirectory);
#ifdef CORE_REGRESSION_TESTS
        if (m_testProcessRunner) {
            worker->setTestProcessRunner(m_testProcessRunner);
        }
        if (!m_testDecryptAssetsDir.isEmpty()) {
            worker->setTestDecryptAssetsDir(m_testDecryptAssetsDir);
        }
#endif
        worker->moveToThread(thread);

        m_worker = worker;
        m_thread = thread;

        connect(thread, &QThread::started, worker,
            [worker, title, savePath, transcodeToMp4]() {
                worker->doWork(title, savePath, transcodeToMp4);
            });
        connect(worker, &DirectFinalizeWorker::finished, this,
            [this, thread](bool ok, const QString& code, const QString& message, const QString& finalPath) {
                m_resultPending = true;
                m_resultOk = ok;
                m_resultCode = code;
                m_resultMessage = message;
                m_resultFinalPath = finalPath;
                thread->quit();
            });
        connect(thread, &QThread::finished, this,
            [this, thread, worker]() {
                const bool shouldEmit = m_thread == thread && m_resultPending;
                const bool resultOk = m_resultOk;
                const QString resultCode = m_resultCode;
                const QString resultMessage = m_resultMessage;
                const QString resultFinalPath = m_resultFinalPath;
                if (m_worker == worker) {
                    m_worker = nullptr;
                }
                if (m_thread == thread) {
                    m_thread = nullptr;
                }
                m_resultPending = false;
                m_resultOk = false;
                m_resultCode.clear();
                m_resultMessage.clear();
                m_resultFinalPath.clear();
                if (shouldEmit) {
                    emit finished(resultOk, resultCode, resultMessage, resultFinalPath);
                }
            });
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);

        thread->start();
    }

    void cancelFinalize() override
    {
        if (m_worker != nullptr) {
            m_worker->cancelFinalize();
        }
    }

    void setTaskDirectory(const QString& taskDirectory)
    {
        m_taskDirectory = taskDirectory;
    }

#ifdef CORE_REGRESSION_TESTS
    void setTestProcessRunner(const std::function<FfmpegCliProcessResult(const FfmpegCliProcessRequest&)>& runner)
    {
        m_testProcessRunner = runner;
    }

    void clearTestProcessRunner()
    {
        m_testProcessRunner = {};
    }

    void setTestDecryptAssetsDir(const QString& decryptAssetsDir)
    {
        m_testDecryptAssetsDir = decryptAssetsDir;
    }

    void clearTestDecryptAssetsDir()
    {
        m_testDecryptAssetsDir.clear();
    }

#endif

private:
    void shutdownActiveStage()
    {
        auto* worker = m_worker;
        auto* thread = m_thread;
        m_worker = nullptr;
        m_thread = nullptr;
        m_resultPending = false;
        m_resultOk = false;
        m_resultCode.clear();
        m_resultMessage.clear();
        m_resultFinalPath.clear();

        if (worker != nullptr) {
            worker->cancelFinalize();
        }
        if (thread != nullptr) {
            thread->quit();
            thread->wait(5000);
        }
    }

    DirectFinalizeWorker* m_worker = nullptr;
    QThread* m_thread = nullptr;
    QString m_taskDirectory;
    bool m_resultPending = false;
    bool m_resultOk = false;
    QString m_resultCode;
    QString m_resultMessage;
    QString m_resultFinalPath;
#ifdef CORE_REGRESSION_TESTS
    std::function<FfmpegCliProcessResult(const FfmpegCliProcessRequest&)> m_testProcessRunner;
    QString m_testDecryptAssetsDir;
#endif
};

namespace {

bool isSharedEnvironmentCode(const QString& value)
{
    return value == QStringLiteral("ffmpeg_missing")
        || value == QStringLiteral("cbox_missing")
        || value == QStringLiteral("license_missing")
        || value == QStringLiteral("output_directory_unavailable")
        || value == QStringLiteral("output_unwritable");
}

bool messageHasSharedEnvironmentCode(const QString& message)
{
    if (isSharedEnvironmentCode(message)) {
        return true;
    }

    static const QStringList codes = {
        QStringLiteral("ffmpeg_missing"),
        QStringLiteral("cbox_missing"),
        QStringLiteral("license_missing"),
        QStringLiteral("output_directory_unavailable"),
        QStringLiteral("output_unwritable")
    };

    for (const QString& code : codes) {
        if (message.contains(code, Qt::CaseInsensitive)) {
            return true;
        }
    }

    return false;
}

DownloadErrorCategory classifyApiResolveFailure(const QString& message)
{
    if (messageHasSharedEnvironmentCode(message)) {
        return DownloadErrorCategory::FileSystemError;
    }

    if (message.startsWith(QStringLiteral("网络请求失败"))
        || message.startsWith(QStringLiteral("网络响应为空"))) {
        return DownloadErrorCategory::NetworkError;
    }

    return DownloadErrorCategory::ValidationError;
}

class ApiServiceResolveBridge final : public CoordinatorResolveService
{
public:
    explicit ApiServiceResolveBridge(APIService* apiService, QObject* parent = nullptr)
        : CoordinatorResolveService(parent)
        , m_apiService(apiService)
    {
        Q_ASSERT(m_apiService != nullptr);

        connect(m_apiService, &APIService::encryptM3U8UrlsResolved, this,
            [this](const QStringList& urls, bool is4K) {
                emit resolved(urls, is4K);
            });
        connect(m_apiService, &APIService::encryptM3U8UrlsFailed, this,
            [this](const QString& errorMessage) {
                emit failed(classifyApiResolveFailure(errorMessage), errorMessage);
            });
        connect(m_apiService, &APIService::encryptM3U8UrlsCancelled, this,
            [this]() {
                emit cancelled();
            });
    }

    void startResolve(const QString& guid, const QString& quality) override
    {
        m_apiService->startGetEncryptM3U8Urls(guid, quality);
    }

    void cancelResolve() override
    {
        m_apiService->cancelGetEncryptM3U8Urls();
    }

private:
    APIService* m_apiService = nullptr;
};

}

DownloadCoordinator::DownloadCoordinator(CoordinatorResolveService* resolveService,
    CoordinatorDownloadStage* downloadStage,
    CoordinatorConcatStage* concatStage,
    CoordinatorDecryptStage* decryptStage,
    CoordinatorDirectFinalizeStage* directFinalizeStage,
    QObject* parent)
    : QObject(parent)
    , m_resolveService(resolveService)
    , m_downloadStage(downloadStage)
    , m_concatStage(concatStage)
    , m_decryptStage(decryptStage)
    , m_directFinalizeStage(directFinalizeStage)
{
    Q_ASSERT(m_resolveService != nullptr);
    Q_ASSERT(m_downloadStage != nullptr);

    if (m_concatStage == nullptr) {
        m_concatStage = new ProductionCoordinatorConcatStage(this);
    }
    if (m_decryptStage == nullptr) {
        m_decryptStage = new ProductionCoordinatorDecryptStage(this);
    }
    if (m_directFinalizeStage == nullptr) {
        m_directFinalizeStage = new ProductionCoordinatorDirectFinalizeStage(this);
    }

    connect(m_resolveService, &CoordinatorResolveService::resolved, this, &DownloadCoordinator::onResolved);
    connect(m_resolveService, &CoordinatorResolveService::failed, this, &DownloadCoordinator::onResolveFailed);
    connect(m_resolveService, &CoordinatorResolveService::cancelled, this, &DownloadCoordinator::onResolveCancelled);
    connect(m_downloadStage, &CoordinatorDownloadStage::downloadProgress, this, &DownloadCoordinator::onDownloadProgress);
    connect(m_downloadStage, &CoordinatorDownloadStage::shardInfoChanged, this, &DownloadCoordinator::onShardInfoChanged);
    connect(m_downloadStage, &CoordinatorDownloadStage::downloadFinished, this, &DownloadCoordinator::onDownloadFinished);
    connect(m_concatStage, &CoordinatorConcatStage::concatFinished, this, &DownloadCoordinator::onConcatFinished);
    connect(m_decryptStage, &CoordinatorDecryptStage::decryptFinished, this, &DownloadCoordinator::onDecryptFinished);
    connect(m_directFinalizeStage, &CoordinatorDirectFinalizeStage::finished, this, &DownloadCoordinator::onDirectFinalizeFinished);
}

DownloadCoordinator::DownloadCoordinator(APIService* apiService,
    CoordinatorDownloadStage* downloadStage,
    CoordinatorConcatStage* concatStage,
    CoordinatorDecryptStage* decryptStage,
    CoordinatorDirectFinalizeStage* directFinalizeStage,
    QObject* parent)
    : QObject(parent)
    , m_resolveService(new ApiServiceResolveBridge(apiService))
    , m_ownedResolveService(m_resolveService)
    , m_downloadStage(downloadStage)
    , m_concatStage(concatStage)
    , m_decryptStage(decryptStage)
    , m_directFinalizeStage(directFinalizeStage)
{
    Q_ASSERT(m_resolveService != nullptr);
    if (m_downloadStage == nullptr) {
        m_ownedDownloadEngine = new DownloadEngine(this);
        m_ownedDownloadStage = new ProductionCoordinatorDownloadStage(m_ownedDownloadEngine, this);
        m_downloadStage = m_ownedDownloadStage;
    }

    if (m_concatStage == nullptr) {
        m_concatStage = new ProductionCoordinatorConcatStage(this);
    }
    if (m_decryptStage == nullptr) {
        m_decryptStage = new ProductionCoordinatorDecryptStage(this);
    }
    if (m_directFinalizeStage == nullptr) {
        m_directFinalizeStage = new ProductionCoordinatorDirectFinalizeStage(this);
    }

    if (m_ownedResolveService != nullptr) {
        m_ownedResolveService->setParent(this);
    }

    connect(m_resolveService, &CoordinatorResolveService::resolved, this, &DownloadCoordinator::onResolved);
    connect(m_resolveService, &CoordinatorResolveService::failed, this, &DownloadCoordinator::onResolveFailed);
    connect(m_resolveService, &CoordinatorResolveService::cancelled, this, &DownloadCoordinator::onResolveCancelled);
    connect(m_downloadStage, &CoordinatorDownloadStage::downloadProgress, this, &DownloadCoordinator::onDownloadProgress);
    connect(m_downloadStage, &CoordinatorDownloadStage::shardInfoChanged, this, &DownloadCoordinator::onShardInfoChanged);
    connect(m_downloadStage, &CoordinatorDownloadStage::downloadFinished, this, &DownloadCoordinator::onDownloadFinished);
    connect(m_concatStage, &CoordinatorConcatStage::concatFinished, this, &DownloadCoordinator::onConcatFinished);
    connect(m_decryptStage, &CoordinatorDecryptStage::decryptFinished, this, &DownloadCoordinator::onDecryptFinished);
    connect(m_directFinalizeStage, &CoordinatorDirectFinalizeStage::finished, this, &DownloadCoordinator::onDirectFinalizeFinished);
}

DownloadCoordinator::DownloadCoordinator(APIService* apiService,
    CoordinatorConcatStage* concatStage,
    CoordinatorDecryptStage* decryptStage,
    CoordinatorDirectFinalizeStage* directFinalizeStage,
    QObject* parent)
    : DownloadCoordinator(apiService,
        nullptr,
        concatStage,
        decryptStage,
        directFinalizeStage,
        parent)
{
}

bool DownloadCoordinator::startSingle(const DownloadJob& job)
{
    return startBatch({ job });
}

bool DownloadCoordinator::startBatch(const QList<DownloadJob>& jobs)
{
    if (m_busy) {
        emit batchBusy();
        return false;
    }

    if (jobs.isEmpty()) {
        return false;
    }

    resetBatchState();
    m_busy = true;
    emit busyChanged(true);

    m_jobs = jobs;
    for (int index = 0; index < m_jobs.size(); ++index) {
        DownloadJob& job = m_jobs[index];
        if (job.id.isEmpty()) {
            job.id = QStringLiteral("job-%1").arg(m_nextGeneratedId++);
        }
        job.state = DownloadJobState::Created;
        job.stage = DownloadJobStage::None;
        job.progressPercent = 0;
        job.errorMessage.clear();
        job.errorCategory = DownloadErrorCategory::Unknown;
        transitionJob(index, DownloadJobState::Queued, DownloadJobStage::None);
    }

    emit batchStarted(m_jobs.size());
    emitBatchProgress();
    scheduleNextQueuedJob();
    return true;
}

void DownloadCoordinator::cancelCurrent()
{
    if (!m_busy) {
        return;
    }

    if (!hasCurrentJob()) {
        for (int index = m_currentIndex + 1; index < m_jobs.size(); ++index) {
            if (m_jobs[index].state != DownloadJobState::Queued) {
                continue;
            }

            DownloadJob& job = m_jobs[index];
            job.errorCategory = DownloadErrorCategory::Cancelled;
            job.errorMessage = QStringLiteral("cancelled");
            transitionJob(index, DownloadJobState::Cancelled, DownloadJobStage::CleaningUp);
            ++m_cancelledJobs;
            emit jobFinished(job);
            emitBatchProgress();
            scheduleNextQueuedJob();
            return;
        }

        finishBatch();
        return;
    }

    m_cancelCurrentRequested = true;
    dispatchCancelForCurrent();
}

void DownloadCoordinator::cancelAll()
{
    if (!m_busy) {
        return;
    }

    m_cancelAllRequested = true;
    markRemainingJobsCancelled(QStringLiteral("cancelled"));
    if (!hasCurrentJob()) {
        finishBatch();
        return;
    }

    dispatchCancelForCurrent();
}

bool DownloadCoordinator::isBusy() const
{
    return m_busy;
}

int DownloadCoordinator::totalJobs() const
{
    return m_jobs.size();
}

int DownloadCoordinator::completedJobs() const
{
    return m_completedJobs;
}

int DownloadCoordinator::failedJobs() const
{
    return m_failedJobs;
}

int DownloadCoordinator::cancelledJobs() const
{
    return m_cancelledJobs;
}

QList<DownloadJob> DownloadCoordinator::jobs() const
{
    return m_jobs;
}

void DownloadCoordinator::onResolved(const QStringList& segmentUrls, bool is4K)
{
    if (!hasCurrentJob()) {
        return;
    }

    m_currentJobIs4K = is4K;
    transitionJob(m_currentIndex, DownloadJobState::Downloading, DownloadJobStage::DownloadingShards);
    m_downloadStage->startDownload(segmentUrls, currentJobTaskDirectory(), currentJobId());
}

void DownloadCoordinator::onResolveFailed(DownloadErrorCategory category, const QString& message)
{
    if (!hasCurrentJob()) {
        return;
    }

    failCurrentJob(category, message);
}

void DownloadCoordinator::onResolveCancelled()
{
    if (!hasCurrentJob()) {
        return;
    }

    cancelCurrentJob();
}

void DownloadCoordinator::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal, const QVariant& userData)
{
    if (!hasCurrentJob() || userData.toString() != currentJobId()) {
        return;
    }

    DownloadJob& job = m_jobs[m_currentIndex];
    job.stage = DownloadJobStage::DownloadingShards;
    if (bytesTotal > 0) {
        const qint64 boundedPercent = (bytesReceived * 100) / bytesTotal;
        job.progressPercent = static_cast<int>(qBound<qint64>(0LL, boundedPercent, 100LL));
    }
    emitCurrentJobChanged();
}

void DownloadCoordinator::onShardInfoChanged(const DownloadInfo& info, const QVariant& userData)
{
    if (!hasCurrentJob() || userData.toString() != currentJobId()) {
        return;
    }

    emit shardInfoChanged(info);
}

void DownloadCoordinator::onDownloadFinished(bool success, const QString& errorString, const QVariant& userData)
{
    if (!hasCurrentJob() || userData.toString() != currentJobId()) {
        return;
    }

    if (!success) {
        if (errorString == QStringLiteral("cancelled")) {
            cancelCurrentJob();
            return;
        }

        failCurrentJob(DownloadErrorCategory::NetworkError, errorString);
        return;
    }

    transitionJob(m_currentIndex, DownloadJobState::Concatenating, DownloadJobStage::MergingShards);
    m_concatStage->setFilePath(currentJobTaskDirectory());
    m_concatStage->startConcat();
}

void DownloadCoordinator::onConcatFinished(bool ok, const QString& message)
{
    if (!hasCurrentJob()) {
        return;
    }

    if (!ok) {
        if (message == QStringLiteral("cancelled")) {
            cancelCurrentJob();
            return;
        }

        failCurrentJob(classifyConcatFailure(message), message);
        return;
    }

    if (m_currentJobIs4K) {
        transitionJob(m_currentIndex, DownloadJobState::DirectFinalizing, DownloadJobStage::PublishingOutput);
        if (auto* productionStage = dynamic_cast<ProductionCoordinatorDirectFinalizeStage*>(m_directFinalizeStage)) {
            productionStage->setTaskDirectory(currentJobTaskDirectory());
        }
        m_directFinalizeStage->startFinalize(m_jobs[m_currentIndex].request.videoTitle,
            m_jobs[m_currentIndex].request.savePath,
            m_jobs[m_currentIndex].request.transcodeToMp4);
        return;
    }

    transitionJob(m_currentIndex, DownloadJobState::Decrypting, DownloadJobStage::RunningDecrypt);
    if (auto* productionStage = dynamic_cast<ProductionCoordinatorDecryptStage*>(m_decryptStage)) {
        productionStage->setTaskDirectory(currentJobTaskDirectory());
    }
    m_decryptStage->setParams(m_jobs[m_currentIndex].request.videoTitle, m_jobs[m_currentIndex].request.savePath);
    m_decryptStage->setTranscodeToMp4(m_jobs[m_currentIndex].request.transcodeToMp4);
    m_decryptStage->startDecrypt();
}

void DownloadCoordinator::onDecryptFinished(bool ok, const QString& message)
{
    if (!hasCurrentJob()) {
        return;
    }

    if (!ok) {
        if (message == QStringLiteral("cancelled")) {
            cancelCurrentJob();
            return;
        }

        failCurrentJob(classifyDecryptFailure(message), message);
        return;
    }

    completeCurrentJob();
}

void DownloadCoordinator::onDirectFinalizeFinished(bool ok, const QString& code, const QString& message, const QString& finalPath)
{
    Q_UNUSED(finalPath);

    if (!hasCurrentJob()) {
        return;
    }

    if (!ok) {
        if (code == QStringLiteral("cancelled") || message == QStringLiteral("cancelled")) {
            cancelCurrentJob();
            return;
        }

        failCurrentJob(classifyDirectFinalizeFailure(code, message), message.isEmpty() ? code : message);
        return;
    }

    completeCurrentJob();
}

void DownloadCoordinator::resetBatchState()
{
    m_jobs.clear();
    m_currentIndex = -1;
    m_completedJobs = 0;
    m_failedJobs = 0;
    m_cancelledJobs = 0;
    m_cancelCurrentRequested = false;
    m_cancelAllRequested = false;
    m_stoppedByFatalError = false;
    m_currentJobIs4K = false;
}

void DownloadCoordinator::startNextQueuedJob()
{
    if (!m_busy) {
        return;
    }

    if (hasCurrentJob()) {
        return;
    }

    if (m_cancelAllRequested || m_stoppedByFatalError) {
        finishBatch();
        return;
    }

    const int nextIndex = m_currentIndex + 1;
    for (int index = nextIndex; index < m_jobs.size(); ++index) {
        if (m_jobs[index].state == DownloadJobState::Queued) {
            m_currentIndex = index;
            m_currentJobIs4K = false;
            if (auto* productionStage = dynamic_cast<ProductionCoordinatorDownloadStage*>(m_downloadStage)) {
                productionStage->setInitialThreadCount(m_jobs[index].request.threadCount);
            }
            transitionJob(index, DownloadJobState::ResolvingM3u8, DownloadJobStage::FetchingPlaylist);
            m_resolveService->startResolve(m_jobs[index].request.url, m_jobs[index].request.quality);
            return;
        }
    }

    finishBatch();
}

void DownloadCoordinator::scheduleNextQueuedJob()
{
    QTimer::singleShot(0, this, &DownloadCoordinator::startNextQueuedJob);
}

void DownloadCoordinator::finishBatch()
{
    if (!m_busy) {
        return;
    }

    const int completedJobs = m_completedJobs;
    const int failedJobs = m_failedJobs;
    const int cancelledJobs = m_cancelledJobs;
    const int totalJobs = m_jobs.size();
    const bool stoppedByFatalError = m_stoppedByFatalError;

    m_currentIndex = -1;
    m_cancelCurrentRequested = false;
    m_cancelAllRequested = false;
    m_currentJobIs4K = false;
    m_busy = false;
    emit batchFinished(completedJobs, failedJobs, cancelledJobs, totalJobs, stoppedByFatalError);
    emit busyChanged(false);
}

void DownloadCoordinator::emitBatchProgress()
{
    emit batchProgress(m_completedJobs, m_failedJobs, m_cancelledJobs, m_jobs.size());
}

void DownloadCoordinator::dispatchCancelForCurrent()
{
    if (!hasCurrentJob()) {
        return;
    }

    switch (m_jobs[m_currentIndex].state) {
    case DownloadJobState::ResolvingM3u8:
        m_resolveService->cancelResolve();
        break;
    case DownloadJobState::Downloading:
        m_downloadStage->cancelAllDownloads();
        break;
    case DownloadJobState::Concatenating:
        m_concatStage->cancelConcat();
        break;
    case DownloadJobState::Decrypting:
        m_decryptStage->cancelDecrypt();
        break;
    case DownloadJobState::DirectFinalizing:
        m_directFinalizeStage->cancelFinalize();
        break;
    default:
        cancelCurrentJob();
        break;
    }
}

void DownloadCoordinator::markRemainingJobsCancelled(const QString& message)
{
    for (int index = m_currentIndex + 1; index < m_jobs.size(); ++index) {
        if (isTerminalState(m_jobs[index].state)) {
            continue;
        }

        m_jobs[index].errorCategory = DownloadErrorCategory::Cancelled;
        m_jobs[index].errorMessage = message;
        transitionJob(index, DownloadJobState::Cancelled, DownloadJobStage::CleaningUp);
        ++m_cancelledJobs;
        emit jobFinished(m_jobs[index]);
    }
    emitBatchProgress();
}

bool DownloadCoordinator::hasCurrentJob() const
{
    return m_currentIndex >= 0 && m_currentIndex < m_jobs.size() && !isTerminalState(m_jobs[m_currentIndex].state);
}

bool DownloadCoordinator::isTerminalState(DownloadJobState state) const
{
    return state == DownloadJobState::Completed
        || state == DownloadJobState::Failed
        || state == DownloadJobState::Cancelled;
}

bool DownloadCoordinator::transitionJob(int index, DownloadJobState newState, DownloadJobStage newStage)
{
    if (index < 0 || index >= m_jobs.size()) {
        return false;
    }

    DownloadJob& job = m_jobs[index];
    if (job.state != newState) {
        if (!isValidTransition(job.state, newState)) {
            return false;
        }
        job.state = newState;
    }
    job.stage = newStage;
    emit jobChanged(job);
    return true;
}

void DownloadCoordinator::emitCurrentJobChanged()
{
    if (!hasCurrentJob()) {
        return;
    }

    emit jobChanged(m_jobs[m_currentIndex]);
}

void DownloadCoordinator::completeCurrentJob()
{
    if (!hasCurrentJob()) {
        return;
    }

    DownloadJob& job = m_jobs[m_currentIndex];
    job.progressPercent = 100;
    transitionJob(m_currentIndex, DownloadJobState::Completed, DownloadJobStage::PublishingOutput);
    ++m_completedJobs;
    emit jobFinished(job);
    emitBatchProgress();
    m_cancelCurrentRequested = false;
    scheduleNextQueuedJob();
}

void DownloadCoordinator::failCurrentJob(DownloadErrorCategory category, const QString& message)
{
    if (!hasCurrentJob()) {
        return;
    }

    DownloadJob& job = m_jobs[m_currentIndex];
    job.errorCategory = category;
    job.errorMessage = message;
    transitionJob(m_currentIndex, DownloadJobState::Failed, DownloadJobStage::CleaningUp);
    ++m_failedJobs;
    emit jobFinished(job);
    emitBatchProgress();
    m_cancelCurrentRequested = false;

    if (classifyFailurePolicy(category) == BatchFailurePolicy::StopBatch) {
        m_stoppedByFatalError = true;
        emit fatalBatchFailure(job, category, message);
        markRemainingJobsCancelled(message.isEmpty() ? QStringLiteral("batch stopped") : QStringLiteral("batch stopped: %1").arg(message));
        finishBatch();
        return;
    }

    scheduleNextQueuedJob();
}

void DownloadCoordinator::cancelCurrentJob(const QString& message)
{
    if (!hasCurrentJob()) {
        return;
    }

    DownloadJob& job = m_jobs[m_currentIndex];
    job.errorCategory = DownloadErrorCategory::Cancelled;
    job.errorMessage = message;
    transitionJob(m_currentIndex, DownloadJobState::Cancelled, DownloadJobStage::CleaningUp);
    ++m_cancelledJobs;
    emit jobFinished(job);
    emitBatchProgress();

    const bool stopBatch = m_cancelAllRequested;
    m_cancelCurrentRequested = false;
    if (stopBatch) {
        finishBatch();
        return;
    }

    scheduleNextQueuedJob();
}

DownloadErrorCategory DownloadCoordinator::classifyConcatFailure(const QString& message) const
{
    if (messageHasSharedEnvironmentCode(message)) {
        return DownloadErrorCategory::FileSystemError;
    }
    return DownloadErrorCategory::ValidationError;
}

DownloadErrorCategory DownloadCoordinator::classifyDecryptFailure(const QString& message) const
{
    if (messageHasSharedEnvironmentCode(message)) {
        return DownloadErrorCategory::FileSystemError;
    }
    return DownloadErrorCategory::DecryptError;
}

DownloadErrorCategory DownloadCoordinator::classifyDirectFinalizeFailure(const QString& code, const QString& message) const
{
    if (isSharedEnvironmentCode(code) || messageHasSharedEnvironmentCode(message)) {
        return DownloadErrorCategory::FileSystemError;
    }
    return DownloadErrorCategory::ValidationError;
}

QString DownloadCoordinator::taskDirectoryForJob(const DownloadJob& job) const
{
    return hashedTaskDirectory(job.request.videoTitle, job.request.savePath, job.id);
}

QString DownloadCoordinator::currentJobTaskDirectory() const
{
    return hasCurrentJob() ? taskDirectoryForJob(m_jobs[m_currentIndex]) : QString();
}

QString DownloadCoordinator::currentJobId() const
{
    return hasCurrentJob() ? m_jobs[m_currentIndex].id : QString();
}

#ifdef CORE_REGRESSION_TESTS
void DownloadCoordinator::setTestDownloadReplyFactory(const std::function<QNetworkReply*(const QNetworkRequest&)>& replyFactory)
{
    auto* productionStage = dynamic_cast<ProductionCoordinatorDownloadStage*>(m_downloadStage);
    if (productionStage != nullptr) {
        productionStage->setTestReplyFactory(replyFactory);
    }
}

void DownloadCoordinator::clearTestDownloadReplyFactory()
{
    auto* productionStage = dynamic_cast<ProductionCoordinatorDownloadStage*>(m_downloadStage);
    if (productionStage != nullptr) {
        productionStage->clearTestReplyFactory();
    }
}

void DownloadCoordinator::setTestDownloadPolicies(int initialTimeoutMs,
    int initialMaxAttempts,
    int initialRetryDelayMs,
    int recoveryTimeoutMs,
    int recoveryMaxAttempts,
    int recoveryRetryDelayMs)
{
    auto* productionStage = dynamic_cast<ProductionCoordinatorDownloadStage*>(m_downloadStage);
    if (productionStage != nullptr) {
        productionStage->setDownloadPolicies(initialTimeoutMs,
            initialMaxAttempts,
            initialRetryDelayMs,
            recoveryTimeoutMs,
            recoveryMaxAttempts,
            recoveryRetryDelayMs);
    }
}

void DownloadCoordinator::setTestDecryptProcessRunner(const std::function<DecryptProcessResult(const DecryptProcessRequest&)>& runner)
{
    auto* productionStage = dynamic_cast<ProductionCoordinatorDecryptStage*>(m_decryptStage);
    if (productionStage != nullptr) {
        productionStage->setTestProcessRunner(runner);
    }
}

void DownloadCoordinator::clearTestDecryptProcessRunner()
{
    auto* productionStage = dynamic_cast<ProductionCoordinatorDecryptStage*>(m_decryptStage);
    if (productionStage != nullptr) {
        productionStage->clearTestProcessRunner();
    }
}

void DownloadCoordinator::setTestDecryptAssetsDir(const QString& decryptAssetsDir)
{
    auto* productionStage = dynamic_cast<ProductionCoordinatorDecryptStage*>(m_decryptStage);
    if (productionStage != nullptr) {
        productionStage->setTestDecryptAssetsDir(decryptAssetsDir);
    }
}

void DownloadCoordinator::clearTestDecryptAssetsDir()
{
    auto* productionStage = dynamic_cast<ProductionCoordinatorDecryptStage*>(m_decryptStage);
    if (productionStage != nullptr) {
        productionStage->clearTestDecryptAssetsDir();
    }
}

void DownloadCoordinator::setTestDecryptStageShutdownWaitMs(int waitMs)
{
    auto* productionStage = dynamic_cast<ProductionCoordinatorDecryptStage*>(m_decryptStage);
    if (productionStage != nullptr) {
        productionStage->setShutdownWaitMs(waitMs);
    }
}

void DownloadCoordinator::setTestDecryptStageLifecycleObserver(const std::function<void(const QString&)>& observer)
{
    auto* productionStage = dynamic_cast<ProductionCoordinatorDecryptStage*>(m_decryptStage);
    if (productionStage != nullptr) {
        productionStage->setLifecycleObserver(observer);
    }
}

void DownloadCoordinator::setTestDirectFinalizeProcessRunner(const std::function<FfmpegCliProcessResult(const FfmpegCliProcessRequest&)>& runner)
{
    auto* productionStage = dynamic_cast<ProductionCoordinatorDirectFinalizeStage*>(m_directFinalizeStage);
    if (productionStage != nullptr) {
        productionStage->setTestProcessRunner(runner);
    }
}

void DownloadCoordinator::clearTestDirectFinalizeProcessRunner()
{
    auto* productionStage = dynamic_cast<ProductionCoordinatorDirectFinalizeStage*>(m_directFinalizeStage);
    if (productionStage != nullptr) {
        productionStage->clearTestProcessRunner();
    }
}

void DownloadCoordinator::setTestDirectFinalizeAssetsDir(const QString& decryptAssetsDir)
{
    auto* productionStage = dynamic_cast<ProductionCoordinatorDirectFinalizeStage*>(m_directFinalizeStage);
    if (productionStage != nullptr) {
        productionStage->setTestDecryptAssetsDir(decryptAssetsDir);
    }
}

void DownloadCoordinator::clearTestDirectFinalizeAssetsDir()
{
    auto* productionStage = dynamic_cast<ProductionCoordinatorDirectFinalizeStage*>(m_directFinalizeStage);
    if (productionStage != nullptr) {
        productionStage->clearTestDecryptAssetsDir();
    }
}
#endif

#include "downloadcoordinator.moc"
