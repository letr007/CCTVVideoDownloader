#include <QtTest>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QLineEdit>
#include <QComboBox>
#include <QDateEdit>
#include <QDate>
#include <QApplication>
#include <QSpinBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QBuffer>
#include <QFile>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QQueue>
#include <QPushButton>
#include <QTableView>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <tuple>

#include "config.h"
#include "setting.h"
#include "apiservice.h"
#include "downloadengine.h"
#include "downloadtask.h"
#include "downloaddialog.h"
#include "directmediafinalizer.h"
#include "decryptworker.h"
#include "ffmpegcliremuxer.h"
#include "mediafinalizer.h"
#include "mediacontainervalidator.h"
#include "concatworker.h"
#include "downloadcoordinator.h"
#include "downloadcoordinatorseams.h"
#include "downloadjob.h"
#include "downloadprogresswindow.h"
#include "tsmerger.h"
#include "import.h"
#include "fakes/fake_networkaccessmanager.h"
#include "fakes/fake_networkreply.h"

Q_DECLARE_METATYPE(DownloadErrorCategory)

namespace {

QString decryptTaskHash(const QString& name)
{
    return QString(QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString coordinatorTaskHash(const QString& title, const QString& jobId)
{
    QString identity = title;
    if (!jobId.isEmpty()) {
        identity += QStringLiteral("\n");
        identity += jobId;
    }
    return QString(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool createEmptyFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.close();
    return true;
}

bool createFakeTsFile(const QString& filePath, int packetCount, quint16 pid = 0)
{
    QByteArray data;
    data.reserve(packetCount * 188);

    for (int i = 0; i < packetCount; ++i) {
        QByteArray packet(188, '\0');
        packet[0] = 0x47;
        packet[1] = static_cast<char>((pid >> 8) & 0x1F);
        packet[2] = static_cast<char>(pid & 0xFF);
        packet[3] = 0x10;
        data.append(packet);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    const qint64 expectedSize = data.size();
    const qint64 written = file.write(data);
    file.close();
    return written == expectedSize;
}

bool createFileWithContents(const QString& filePath, const QByteArray& data)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    const qint64 written = file.write(data);
    file.close();
    return written == data.size();
}

QByteArray createFakeMp4Bytes()
{
    QByteArray data;
    data.append(char(0x00));
    data.append(char(0x00));
    data.append(char(0x00));
    data.append(char(0x18));
    data.append("ftyp", 4);
    data.append("isom", 4);
    data.append(char(0x00));
    data.append(char(0x00));
    data.append(char(0x00));
    data.append(char(0x00));
    data.append("isom", 4);
    data.append("mp42", 4);
    return data;
}

void createDecryptAssets(const QString& assetsDirPath, bool includeCboxExe = true, bool includeLicense = true)
{
    if (includeCboxExe) {
        QVERIFY(createEmptyFile(QDir(assetsDirPath).filePath("cbox.exe")));
    }
    if (includeLicense) {
        QVERIFY(createEmptyFile(QDir(assetsDirPath).filePath("UDRM_LICENSE.v1.0")));
    }
}

QString bundledFfmpegPath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath("decrypt/ffmpeg.exe");
}

QByteArray createRemuxableTsFixtureBytes()
{
    static const QByteArray encoded =
        "R0AREABC8CUAAcEAAP8B/wAB/IAUSBIBBkZGbXBlZwlTZXJ2aWNlMDF3fEPK////////////////////"
        "////////////////////////////////////////////////////////////////////////////////"
        "////////////////////////////////////////////////////////////////////////////////"
        "//////////9HQAAQAACwDQABwQAAAAHwACqxBLL/////////////////////////////////////////"
        "////////////////////////////////////////////////////////////////////////////////"
        "////////////////////////////////////////////////////////////////////////////////"
        "/////////////////////0dQABAAArAXAAHBAADhAPAAAuEA8AAD4QHwAPZKA1X/////////////////"
        "////////////////////////////////////////////////////////////////////////////////"
        "////////////////////////////////////////////////////////////////////////////////"
        "////////////////////////////////R0EAMAdQAAB7DH4AAAAB4AAAgMAKMQAH9IERAAfYYQAAAbMU"
        "APAj///gGAAAAbUUigABAAAAAAG4AAgAQAAAAQAAD//4AAABtY//80GAAAABARP5RSlL9wvNuUpSIuUp"
        "SIuUpSIuUpSIuUpSIuUpSIuUpSIuUpSIuUpSIuUpSIuUpSIuUpSIuUpSIuUpSIuUpSIuUpSIuUpSIuUp"
        "SIuUpSIgAAABAhP5RSlL9wvNuUpSIuUpSIuUpSIuUpQ=";
    return QByteArray::fromBase64(encoded);
}

DownloadJob makeCoordinatorJob(const QString& id,
    const QString& guid,
    const QString& title,
    const QString& quality,
    const QString& savePath)
{
    DownloadJob job;
    job.id = id;
    job.request.url = guid;
    job.request.videoTitle = title;
    job.request.quality = quality;
    job.request.savePath = savePath;
    return job;
}

}

void setDownloadTaskTestFileWriteHook(const std::function<qint64(QFile&, const QByteArray&)>& hook);
void clearDownloadTaskTestFileWriteHook();
void setDownloadTaskTestRenameHook(const std::function<bool(const QString&, const QString&)>& hook);
void clearDownloadTaskTestRenameHook();

class APIServiceTestAdapter {
public:
    static QByteArray sendNetworkRequest(APIService& apiService, const QUrl& url, const QHash<QString, QString>& headers = {})
    {
        return apiService.sendNetworkRequest(url, headers);
    }

    static QJsonObject parseJsonObject(APIService& apiService, const QByteArray& data, const QString& key = QString())
    {
        return apiService.parseJsonObject(data, key);
    }

    static QJsonArray parseJsonArray(APIService& apiService, const QByteArray& data, const QString& objectKey = QString(), const QString& arrayKey = QString())
    {
        return apiService.parseJsonArray(data, objectKey, arrayKey);
    }

    static void processMonthData(APIService& apiService, const QJsonArray& items, const QString& month, QMap<int, VideoItem>& result, int& resultIndex)
    {
        apiService.processMonthData(items, month, result, resultIndex);
    }

    static void processMonthData(APIService& apiService, const QJsonArray& items, const QString& month, QMap<int, VideoItem>& result, int& resultIndex, bool isHighlight)
    {
        apiService.processMonthData(items, month, result, resultIndex, isHighlight);
    }

    static void processTopicVideoData(APIService& apiService, const QJsonArray& items, QMap<int, VideoItem>& result, int& resultIndex)
    {
        apiService.processTopicVideoData(items, result, resultIndex);
    }

    static QHash<QString, QString> parseM3U8QualityUrls(APIService& apiService, const QByteArray& m3u8Data, const QString& baseUrl)
    {
        return apiService.parseM3U8QualityUrls(m3u8Data, baseUrl);
    }

    static QString selectQuality(APIService& apiService, const QString& requestedQuality, const QHash<QString, QString>& availableQualities)
    {
        return apiService.selectQuality(requestedQuality, availableQualities);
    }

    static QUrl buildVideoApiUrl(APIService& apiService, FetchType fetchType, const QString& id, const QString& date, int page, int pageSize)
    {
        return apiService.buildVideoApiUrl(fetchType, id, date, page, pageSize);
    }

    static QUrl buildAlbumVideoListUrl(APIService& apiService, const QString& albumId, int mode, int page, int pageSize)
    {
        return apiService.buildAlbumVideoListUrl(albumId, mode, page, pageSize);
    }

    static QUrl buildTopicVideoListUrl(APIService& apiService, const QString& columnId, const QString& itemId, int type)
    {
        return apiService.buildTopicVideoListUrl(columnId, itemId, type);
    }

    static QStringList buildTsUrlsFromPlaylistData(APIService& apiService, const QByteArray& playlistData, const QString& fullM3u8Url)
    {
        return apiService.buildTsUrlsFromPlaylistData(playlistData, fullM3u8Url);
    }

    static QStringList getTsFileList(APIService& apiService, const QString& qualityPath, const QString& baseUrl)
    {
        return apiService.getTsFileList(qualityPath, baseUrl);
    }

    static void setTestNetworkAccessManager(APIService& apiService, QNetworkAccessManager* networkAccessManager)
    {
        apiService.setTestNetworkAccessManager(networkAccessManager);
    }

    static void clearTestNetworkAccessManager(APIService& apiService)
    {
        apiService.clearTestNetworkAccessManager();
    }

    static QStringList getEncryptM3U8Urls(APIService& apiService, const QString& guid, const QString& quality)
    {
        return apiService.getEncryptM3U8Urls(guid, quality);
    }
};

class DownloadTaskTestAdapter {
public:
    static void setTestNetworkAccessManager(DownloadTask& task, QNetworkAccessManager* networkAccessManager)
    {
        task.setTestNetworkAccessManager(networkAccessManager);
    }

    static void clearTestNetworkAccessManager(DownloadTask& task)
    {
        task.clearTestNetworkAccessManager();
    }

    static int timeoutMs(const DownloadTask& task)
    {
        return task.m_timeoutMs;
    }

    static int maxAttempts(const DownloadTask& task)
    {
        return task.m_maxAttempts;
    }

    static int retryDelayMs(const DownloadTask& task)
    {
        return task.m_retryDelayMs;
    }
};

class DownloadEngineTestAdapter {
public:
    static int defaultTimeoutMs(const DownloadEngine& engine)
    {
        return engine.m_defaultTimeoutMs;
    }

    static int defaultMaxAttempts(const DownloadEngine& engine)
    {
        return engine.m_defaultMaxAttempts;
    }

    static int defaultRetryDelayMs(const DownloadEngine& engine)
    {
        return engine.m_defaultRetryDelayMs;
    }

    static void setTestReplyFactory(DownloadEngine& engine, const std::function<QNetworkReply*(const QNetworkRequest&)>& replyFactory)
    {
        engine.setTestReplyFactory(replyFactory);
    }

    static void clearTestReplyFactory(DownloadEngine& engine)
    {
        engine.clearTestReplyFactory();
    }
};

class DownloadDialogTestAdapter {
public:
    static void setTestReplyFactory(Download& dialog, const std::function<QNetworkReply*(const QNetworkRequest&)>& replyFactory)
    {
        dialog.setTestReplyFactory(replyFactory);
    }

    static void clearTestReplyFactory(Download& dialog)
    {
        dialog.clearTestReplyFactory();
    }

    static void setTestDownloadPolicies(Download& dialog,
        int initialTimeoutMs,
        int initialMaxAttempts,
        int initialRetryDelayMs,
        int recoveryTimeoutMs,
        int recoveryMaxAttempts,
        int recoveryRetryDelayMs)
    {
        dialog.setTestDownloadPolicies(initialTimeoutMs,
            initialMaxAttempts,
            initialRetryDelayMs,
            recoveryTimeoutMs,
            recoveryMaxAttempts,
            recoveryRetryDelayMs);
    }
};

class DecryptWorkerTestAdapter {
public:
    static void setProcessTimeoutMs(DecryptWorker& worker, int timeoutMs)
    {
        worker.setProcessTimeoutMs(timeoutMs);
    }

    static void setTranscodeToMp4(DecryptWorker& worker, bool transcodeToMp4)
    {
        worker.setTranscodeToMp4(transcodeToMp4);
    }

    static void setTestProcessRunner(DecryptWorker& worker, const std::function<DecryptProcessResult(const DecryptProcessRequest&)>& runner)
    {
        worker.setTestProcessRunner(runner);
    }

    static void clearTestProcessRunner(DecryptWorker& worker)
    {
        worker.clearTestProcessRunner();
    }

    static void setTestDecryptAssetsDir(DecryptWorker& worker, const QString& decryptAssetsDir)
    {
        worker.setTestDecryptAssetsDir(decryptAssetsDir);
    }

    static void clearTestDecryptAssetsDir(DecryptWorker& worker)
    {
        worker.clearTestDecryptAssetsDir();
    }
};

class MediaFinalizerTestAdapter {
public:
    static void setProcessTimeoutMs(MediaFinalizer& finalizer, int timeoutMs)
    {
        finalizer.setProcessTimeoutMs(timeoutMs);
    }

    static void setTestProcessRunner(MediaFinalizer& finalizer,
        const std::function<FfmpegCliProcessResult(const FfmpegCliProcessRequest&)>& runner)
    {
        finalizer.m_remuxer.setTestProcessRunner(runner);
    }

    static void clearTestProcessRunner(MediaFinalizer& finalizer)
    {
        finalizer.m_remuxer.clearTestProcessRunner();
    }

    static void setTestDecryptAssetsDir(MediaFinalizer& finalizer, const QString& decryptAssetsDir)
    {
        finalizer.m_remuxer.setTestDecryptAssetsDir(decryptAssetsDir);
    }

    static void clearTestDecryptAssetsDir(MediaFinalizer& finalizer)
    {
        finalizer.m_remuxer.clearTestDecryptAssetsDir();
    }
};

class DownloadCoordinatorTestAdapter {
public:
    static void setTestDownloadReplyFactory(DownloadCoordinator& coordinator, const std::function<QNetworkReply*(const QNetworkRequest&)>& replyFactory)
    {
        coordinator.setTestDownloadReplyFactory(replyFactory);
    }

    static void clearTestDownloadReplyFactory(DownloadCoordinator& coordinator)
    {
        coordinator.clearTestDownloadReplyFactory();
    }

    static void setTestDownloadPolicies(DownloadCoordinator& coordinator,
        int initialTimeoutMs,
        int initialMaxAttempts,
        int initialRetryDelayMs,
        int recoveryTimeoutMs,
        int recoveryMaxAttempts,
        int recoveryRetryDelayMs)
    {
        coordinator.setTestDownloadPolicies(initialTimeoutMs,
            initialMaxAttempts,
            initialRetryDelayMs,
            recoveryTimeoutMs,
            recoveryMaxAttempts,
            recoveryRetryDelayMs);
    }

    static void setTestDecryptProcessRunner(DownloadCoordinator& coordinator,
        const std::function<DecryptProcessResult(const DecryptProcessRequest&)>& runner)
    {
        coordinator.setTestDecryptProcessRunner(runner);
    }

    static void clearTestDecryptProcessRunner(DownloadCoordinator& coordinator)
    {
        coordinator.clearTestDecryptProcessRunner();
    }

    static void setTestDecryptAssetsDir(DownloadCoordinator& coordinator, const QString& decryptAssetsDir)
    {
        coordinator.setTestDecryptAssetsDir(decryptAssetsDir);
    }

    static void clearTestDecryptAssetsDir(DownloadCoordinator& coordinator)
    {
        coordinator.clearTestDecryptAssetsDir();
    }

    static void setTestDecryptStageShutdownWaitMs(DownloadCoordinator& coordinator, int waitMs)
    {
        coordinator.setTestDecryptStageShutdownWaitMs(waitMs);
    }

    static void setTestDecryptStageLifecycleObserver(DownloadCoordinator& coordinator,
        const std::function<void(const QString&)>& observer)
    {
        coordinator.setTestDecryptStageLifecycleObserver(observer);
    }

    static void setTestDirectFinalizeProcessRunner(DownloadCoordinator& coordinator,
        const std::function<FfmpegCliProcessResult(const FfmpegCliProcessRequest&)>& runner)
    {
        coordinator.setTestDirectFinalizeProcessRunner(runner);
    }

    static void clearTestDirectFinalizeProcessRunner(DownloadCoordinator& coordinator)
    {
        coordinator.clearTestDirectFinalizeProcessRunner();
    }

    static void setTestDirectFinalizeAssetsDir(DownloadCoordinator& coordinator, const QString& decryptAssetsDir)
    {
        coordinator.setTestDirectFinalizeAssetsDir(decryptAssetsDir);
    }

    static void clearTestDirectFinalizeAssetsDir(DownloadCoordinator& coordinator)
    {
        coordinator.clearTestDirectFinalizeAssetsDir();
    }
};

class DirectFinalizeWorkerTestAdapter {
public:
    static void setTestProcessRunner(DirectFinalizeWorker& worker,
        const std::function<FfmpegCliProcessResult(const FfmpegCliProcessRequest&)>& runner)
    {
        worker.setTestProcessRunner(runner);
    }

    static void clearTestProcessRunner(DirectFinalizeWorker& worker)
    {
        worker.clearTestProcessRunner();
    }

    static void setTestDecryptAssetsDir(DirectFinalizeWorker& worker, const QString& decryptAssetsDir)
    {
        worker.setTestDecryptAssetsDir(decryptAssetsDir);
    }

    static void clearTestDecryptAssetsDir(DirectFinalizeWorker& worker)
    {
        worker.clearTestDecryptAssetsDir();
    }
};

enum class FakeCoordinatorOutcome {
    Success,
    Failure,
    Cancelled
};

struct FakeResolveAction {
    FakeCoordinatorOutcome outcome = FakeCoordinatorOutcome::Success;
    QStringList segmentUrls;
    bool is4K = false;
    DownloadErrorCategory category = DownloadErrorCategory::Unknown;
    QString message;
};

struct FakeDownloadAction {
    FakeCoordinatorOutcome outcome = FakeCoordinatorOutcome::Success;
    QList<QPair<qint64, qint64>> progressSteps;
    QString errorString;
};

struct FakeConcatAction {
    FakeCoordinatorOutcome outcome = FakeCoordinatorOutcome::Success;
    QString message;
    int delayMs = 0;
};

struct FakeDecryptAction {
    FakeCoordinatorOutcome outcome = FakeCoordinatorOutcome::Success;
    QString message;
    int delayMs = 0;
};

struct FakeDirectFinalizeAction {
    FakeCoordinatorOutcome outcome = FakeCoordinatorOutcome::Success;
    QString code;
    QString message;
    QString finalPath;
    int delayMs = 0;
};

class FakeCoordinatorResolveService : public CoordinatorResolveService
{
    Q_OBJECT

public:
    explicit FakeCoordinatorResolveService(QObject* parent = nullptr)
        : CoordinatorResolveService(parent)
    {
    }

    void queueSuccess(const QStringList& segmentUrls, bool is4K)
    {
        FakeResolveAction action;
        action.outcome = FakeCoordinatorOutcome::Success;
        action.segmentUrls = segmentUrls;
        action.is4K = is4K;
        m_actions.enqueue(action);
    }

    void queueFailure(DownloadErrorCategory category, const QString& message)
    {
        FakeResolveAction action;
        action.outcome = FakeCoordinatorOutcome::Failure;
        action.category = category;
        action.message = message;
        m_actions.enqueue(action);
    }

    void queueCancelled()
    {
        FakeResolveAction action;
        action.outcome = FakeCoordinatorOutcome::Cancelled;
        m_actions.enqueue(action);
    }

    void startResolve(const QString& guid, const QString& quality) override
    {
        QVERIFY(!m_actions.isEmpty());
        m_lastGuid = guid;
        m_lastQuality = quality;
        m_pending = true;

        const FakeResolveAction action = m_actions.dequeue();
        QTimer::singleShot(0, this, [this, action]() {
            if (!m_pending) {
                return;
            }

            m_pending = false;
            switch (action.outcome) {
            case FakeCoordinatorOutcome::Success:
                emit resolved(action.segmentUrls, action.is4K);
                break;
            case FakeCoordinatorOutcome::Failure:
                emit failed(action.category, action.message);
                break;
            case FakeCoordinatorOutcome::Cancelled:
                emit cancelled();
                break;
            }
        });
    }

    void cancelResolve() override
    {
        if (!m_pending) {
            return;
        }

        m_pending = false;
        emit cancelled();
    }

    QString lastGuid() const { return m_lastGuid; }
    QString lastQuality() const { return m_lastQuality; }

private:
    QQueue<FakeResolveAction> m_actions;
    QString m_lastGuid;
    QString m_lastQuality;
    bool m_pending = false;
};

class FakeCoordinatorDownloadStage : public CoordinatorDownloadStage
{
    Q_OBJECT

public:
    explicit FakeCoordinatorDownloadStage(QObject* parent = nullptr)
        : CoordinatorDownloadStage(parent)
    {
    }

    void queueSuccess(const QList<QPair<qint64, qint64>>& progressSteps)
    {
        FakeDownloadAction action;
        action.outcome = FakeCoordinatorOutcome::Success;
        action.progressSteps = progressSteps;
        m_actions.enqueue(action);
    }

    void queueFailure(const QList<QPair<qint64, qint64>>& progressSteps, const QString& errorString)
    {
        FakeDownloadAction action;
        action.outcome = FakeCoordinatorOutcome::Failure;
        action.progressSteps = progressSteps;
        action.errorString = errorString;
        m_actions.enqueue(action);
    }

    void queueCancelled(const QList<QPair<qint64, qint64>>& progressSteps = {})
    {
        FakeDownloadAction action;
        action.outcome = FakeCoordinatorOutcome::Cancelled;
        action.progressSteps = progressSteps;
        action.errorString = QStringLiteral("cancelled");
        m_actions.enqueue(action);
    }

    void startDownload(const QStringList& segmentUrls, const QString& saveDir, const QVariant& userData) override
    {
        QVERIFY(!m_actions.isEmpty());
        m_lastUrls = segmentUrls;
        m_lastSaveDir = saveDir;
        m_activeUserData = userData;
        m_pending = true;

        const FakeDownloadAction action = m_actions.dequeue();
        QTimer::singleShot(0, this, [this, action, userData]() {
            if (!m_pending || m_activeUserData != userData) {
                return;
            }

            for (int index = 0; index < m_lastUrls.size(); ++index) {
                emit shardInfoChanged(DownloadInfo(index + 1,
                    DownloadStatus::Waiting,
                    m_lastUrls.at(index),
                    0),
                    userData);
            }

            for (const auto& step : action.progressSteps) {
                emit downloadProgress(step.first, step.second, userData);
            }

            if (!m_pending || m_activeUserData != userData) {
                return;
            }

            m_pending = false;
            switch (action.outcome) {
            case FakeCoordinatorOutcome::Success:
                emit downloadFinished(true, QString(), userData);
                break;
            case FakeCoordinatorOutcome::Failure:
                emit downloadFinished(false, action.errorString, userData);
                break;
            case FakeCoordinatorOutcome::Cancelled:
                emit downloadFinished(false, QStringLiteral("cancelled"), userData);
                break;
            }
            emit allDownloadFinished();
        });
    }

    void cancelDownload(const QVariant& userData) override
    {
        if (!m_pending || m_activeUserData != userData) {
            return;
        }

        m_pending = false;
        emit downloadFinished(false, QStringLiteral("cancelled"), userData);
        emit allDownloadFinished();
    }

    void cancelAllDownloads() override
    {
        if (!m_pending) {
            return;
        }

        const QVariant userData = m_activeUserData;
        m_pending = false;
        emit downloadFinished(false, QStringLiteral("cancelled"), userData);
        emit allDownloadFinished();
    }

    QStringList lastUrls() const { return m_lastUrls; }
    QString lastSaveDir() const { return m_lastSaveDir; }

private:
    QQueue<FakeDownloadAction> m_actions;
    QStringList m_lastUrls;
    QString m_lastSaveDir;
    QVariant m_activeUserData;
    bool m_pending = false;
};

class FakeCoordinatorConcatStage : public CoordinatorConcatStage
{
    Q_OBJECT

public:
    explicit FakeCoordinatorConcatStage(QObject* parent = nullptr)
        : CoordinatorConcatStage(parent)
    {
    }

    void queueSuccess(const QString& message, int delayMs = 0)
    {
        m_actions.enqueue({FakeCoordinatorOutcome::Success, message, delayMs});
    }

    void queueFailure(const QString& message, int delayMs = 0)
    {
        m_actions.enqueue({FakeCoordinatorOutcome::Failure, message, delayMs});
    }

    void queueCancelled(const QString& message = QStringLiteral("cancelled"), int delayMs = 0)
    {
        m_actions.enqueue({FakeCoordinatorOutcome::Cancelled, message, delayMs});
    }

    void setFilePath(const QString& path) override
    {
        m_filePath = path;
    }

    void startConcat() override
    {
        QVERIFY(!m_actions.isEmpty());
        ++m_startCount;
        m_pending = true;
        const FakeConcatAction action = m_actions.dequeue();
        QTimer::singleShot(action.delayMs, this, [this, action]() {
            if (!m_pending) {
                return;
            }

            m_pending = false;
            emit concatFinished(action.outcome == FakeCoordinatorOutcome::Success, action.message);
        });
    }

    void cancelConcat() override
    {
        if (!m_pending) {
            return;
        }

        m_pending = false;
        emit concatFinished(false, QStringLiteral("cancelled"));
    }

    QString filePath() const { return m_filePath; }
    int startCount() const { return m_startCount; }

private:
    QQueue<FakeConcatAction> m_actions;
    QString m_filePath;
    int m_startCount = 0;
    bool m_pending = false;
};

class FakeCoordinatorDecryptStage : public CoordinatorDecryptStage
{
    Q_OBJECT

public:
    explicit FakeCoordinatorDecryptStage(QObject* parent = nullptr)
        : CoordinatorDecryptStage(parent)
    {
    }

    void queueSuccess(const QString& message, int delayMs = 0)
    {
        m_actions.enqueue({FakeCoordinatorOutcome::Success, message, delayMs});
    }

    void queueFailure(const QString& message, int delayMs = 0)
    {
        m_actions.enqueue({FakeCoordinatorOutcome::Failure, message, delayMs});
    }

    void queueCancelled(const QString& message = QStringLiteral("cancelled"), int delayMs = 0)
    {
        m_actions.enqueue({FakeCoordinatorOutcome::Cancelled, message, delayMs});
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

    void startDecrypt() override
    {
        QVERIFY(!m_actions.isEmpty());
        m_pending = true;
        const FakeDecryptAction action = m_actions.dequeue();
        QTimer::singleShot(action.delayMs, this, [this, action]() {
            if (!m_pending) {
                return;
            }

            m_pending = false;
            emit decryptFinished(action.outcome == FakeCoordinatorOutcome::Success, action.message);
        });
    }

    void cancelDecrypt() override
    {
        if (!m_pending) {
            return;
        }

        m_pending = false;
        emit decryptFinished(false, QStringLiteral("cancelled"));
    }

    QString name() const { return m_name; }
    QString savePath() const { return m_savePath; }
    bool transcodeToMp4() const { return m_transcodeToMp4; }

private:
    QQueue<FakeDecryptAction> m_actions;
    QString m_name;
    QString m_savePath;
    bool m_transcodeToMp4 = false;
    bool m_pending = false;
};

class FakeCoordinatorDirectFinalizeStage : public CoordinatorDirectFinalizeStage
{
    Q_OBJECT

public:
    explicit FakeCoordinatorDirectFinalizeStage(QObject* parent = nullptr)
        : CoordinatorDirectFinalizeStage(parent)
    {
    }

    void queueSuccess(const QString& code, const QString& message, const QString& finalPath, int delayMs = 0)
    {
        FakeDirectFinalizeAction action;
        action.outcome = FakeCoordinatorOutcome::Success;
        action.code = code;
        action.message = message;
        action.finalPath = finalPath;
        action.delayMs = delayMs;
        m_actions.enqueue(action);
    }

    void queueFailure(const QString& code, const QString& message, int delayMs = 0)
    {
        FakeDirectFinalizeAction action;
        action.outcome = FakeCoordinatorOutcome::Failure;
        action.code = code;
        action.message = message;
        action.delayMs = delayMs;
        m_actions.enqueue(action);
    }

    void queueCancelled(const QString& message = QStringLiteral("cancelled"), int delayMs = 0)
    {
        FakeDirectFinalizeAction action;
        action.outcome = FakeCoordinatorOutcome::Cancelled;
        action.code = QStringLiteral("cancelled");
        action.message = message;
        action.delayMs = delayMs;
        m_actions.enqueue(action);
    }

    void startFinalize(const QString& title, const QString& savePath, bool transcodeToMp4) override
    {
        QVERIFY(!m_actions.isEmpty());
        m_title = title;
        m_savePath = savePath;
        m_transcodeToMp4 = transcodeToMp4;
        m_pending = true;

        const FakeDirectFinalizeAction action = m_actions.dequeue();
        QTimer::singleShot(action.delayMs, this, [this, action]() {
            if (!m_pending) {
                return;
            }

            m_pending = false;
            emit finished(action.outcome == FakeCoordinatorOutcome::Success,
                action.code,
                action.message,
                action.finalPath);
        });
    }

    void cancelFinalize() override
    {
        if (!m_pending) {
            return;
        }

        m_pending = false;
        emit finished(false, QStringLiteral("cancelled"), QStringLiteral("cancelled"), QString());
    }

    QString title() const { return m_title; }
    QString savePath() const { return m_savePath; }
    bool transcodeToMp4() const { return m_transcodeToMp4; }

private:
    QQueue<FakeDirectFinalizeAction> m_actions;
    QString m_title;
    QString m_savePath;
    bool m_transcodeToMp4 = false;
    bool m_pending = false;
};

class CoreRegressionTests : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void initGlobalSettings_createsDefaults();
    void setting_saveSettings_roundTripsValuesFromWidgetsToDisk();
    void setting_smoke_persistsSettingsAcrossFreshSession();

    void downloadTask_cancelBeforeRun_emitsCancelledSignal();
    void downloadTask_fileOpenFailure_emitsCannotOpenFileSignal();
    void downloadTask_fakeNetwork_success_writesExactBytes();
    void downloadTask_fakeNetwork_error_emitsFailureOnce();
    void downloadTask_defaultNetworkError_noRetry_preservesLegacyErrorString();
    void downloadTask_cancelWhileReplyPending_emitsSingleCancelledCompletion();
    void downloadTask_delayedFakeReply_completesWithinBoundedTime();
    void downloadTask_timeoutWhileReplyStalls_emitsStructuredTimeoutOnce();
    void downloadTask_completedPayloadBeforeDelayedFinish_succeedsWithoutTimeout();
    void downloadTask_cancelWhileReplyPending_withTimeoutEnabled_preservesCancellation();
    void downloadTask_retryOnNetworkError_thenSucceeds_writesOnlyFinalBytes();
    void downloadTask_retryOnNetworkError_exhausted_emitsStructuredFailure();
    void downloadTask_retryOnTimeout_exhausted_emitsStructuredTimeoutFailure();
    void downloadTask_cancelDuringRetryDelay_doesNotIssueSecondRequest();
    void downloadTask_integrity_success_publishesFinalTs();
    void downloadTask_integrity_publishFailure_preservesExistingFinalTs();
    void downloadTask_integrity_emptyNoError_failsWithNoFiles();
    void downloadTask_integrity_sizeMismatch_failsWithNoFiles();
    void downloadTask_integrity_shortWrite_failsImmediately();
    void downloadTask_integrity_retryAfterPartial_hasOnlyFinalContent();
    void downloadTask_integrity_unknownLengthNonEmpty_succeeds();
    void downloadTask_integrity_cancelBeforeAndDuring_leavesNoFiles();
    void downloadEngine_idleConstructionDestruction_isSafe();
    void downloadEngine_fakeNetwork_success_emitsDownloadAndAllFinished();
    void downloadEngine_fakeNetwork_error_emitsFailureAndAllFinished();
    void downloadEngine_cancelActiveTask_emitsSingleCancelledFailureAndAllFinished();
    void downloadEngine_cancelWhenIdle_isSafeNoOp();
    void downloadEngine_defaults_enableTimeoutAndRetry();
    void downloadEngine_retrySettings_propagateToTask();
    void downloadDialog_recoveryPhase_rescuesFailedShardSerially();
    void downloadDialog_persistedPendingShards_resumeOnlyMissingPieces();
    void downloadDialog_withoutStateFile_redownloadsExistingShard();
    void tsMerger_mergeCreatesOutputInTaskDirectory();

    void decryptWorker_renameFailure_emitsRenameError();
    void decryptWorker_fakeProcessRunner_seamSkipsRealProcess();
    void decryptWorker_cboxOutputStaysInTaskDirectoryAndPreservesSaveRootResultTs();
    void decryptWorker_cleanupFailure_emitsSingleFailureAfterPublish();
    void decryptWorker_processTimeoutNormalization_passesDefaultTimeoutToRunner();
    void decryptWorker_testDecryptAssetsDir_usesTemporaryAssets();
    void decryptWorker_invalidParams_emitsStructuredPreflightError();
    void decryptWorker_missingInput_emitsPreflightErrorBeforeMutation();
    void decryptWorker_missingCbox_emitsPreflightErrorBeforeProcessStart();
    void decryptWorker_missingLicense_emitsPreflightErrorBeforeProcessStart();
    void decryptWorker_outputUnwritable_emitsPreflightErrorBeforeProcessStart();
    void decryptWorker_startFailed_emitsStructuredProcessStartError();
    void decryptWorker_timedOut_emitsStructuredTimeoutError();
    void decryptWorker_processFailed_prefersStderrAndCleansUpArtifacts();
    void decryptWorker_processFailed_fallsBackToStdoutDiagnostic();
    void decryptWorker_processFailed_truncatesLongDiagnostic();
    void decryptWorker_processFailed_restoresTempResultMp4();
    void decryptWorker_processFailed_preservesPreExistingLicense();
    void decryptWorker_success_preservesPreExistingLicense();
    void decryptWorker_success_canKeepDecryptedTs();
    void decryptWorker_invalidCboxOutput_rejectsAndDoesNotPublish();
    void decryptWorker_crashExitWithZeroExitCode_emitsProcessFailure();
    void decryptWorker_cancelDuringProcess_emitsCancelledAndDoesNotPublish();

    void concatWorker_success_stagesResultTs();
    void concatWorker_zeroByteFile_emitsFailure();
    void concatWorker_cancelDuringMerge_emitsCancelledAndDoesNotStageResultTs();

    void tsMerger_validMinimalPacket_succeeds();
    void tsMerger_zeroByteFile_returnsFalse();
    void tsMerger_malformedNonZeroFile_returnsFalse();
    void tsMerger_failedMerge_preservesExistingOutputFile();
    void mediaContainerValidator_validTs_detectsMpegTs();
    void mediaContainerValidator_validMp4_detectsMp4();
    void mediaContainerValidator_tsBytesRenamedMp4_rejectsAsMp4();
    void mediaContainerValidator_mixedFtypAndTsSync_rejectsAsMp4();
    void mediaContainerValidator_invalidFiles_rejectEmptyAndUnknown();
    void directMediaFinalizer_whitespaceTitle_usesProducerHashContract();
    void cctvVideoDownloader_cctv4kTsSelection_finalizesStagedTs();
    void cctvVideoDownloader_cctv4kMp4Selection_remuxesStagedTs();
    void mediaFinalizer_publishTs_validatesAndUsesUniqueName();
    void mediaFinalizer_remuxesToMp4ThroughBundledCli();
    void mediaFinalizer_missingBundledFfmpeg_reportsFailure();
    void mediaFinalizer_remuxTimeout_reportsFailureAndDoesNotPublishMp4();
    void mediaFinalizer_remuxCancel_reportsCancelledAndDoesNotPublishMp4();
    void mediaFinalizer_remuxProcessFailure_reportsDiagnostic();
    void mediaFinalizer_invalidRemuxedMp4_reportsValidationFailure();
    void directFinalizeWorker_cancelDuringRemux_emitsCancelledAndDoesNotPublish();

    // ── DownloadJob contract tests ───────────────────────────
    void downloadJob_legalStateTransitions_acceptsExpectedSequence();
    void downloadJob_illegalStateTransitions_rejectsInvalidSequences();
    void downloadJob_failurePolicy_classifiesVideoSpecificErrors_asSkipVideo();
    void downloadJob_failurePolicy_classifiesSharedEnvironmentErrors_asStopBatch();

    void coordinatorFakeResolveService_supportsSuccessFailureAndCancel();
    void coordinatorFakeDownloadStage_supportsProgressFailureAndCancel();
    void coordinatorFakeConcatStage_supportsSuccessFailureAndCancel();
    void coordinatorFakeDecryptStage_supportsSuccessFailureAndCancel();
    void coordinatorFakeDirectFinalizeStage_supportsSuccessFailureAndCancel();
    void downloadCoordinator_batchSuccess_processesJobsInOrder();
    void downloadCoordinator_normalJob_emitsConcatThenDecryptSequence();
    void downloadCoordinator_4kJob_emitsConcatThenDirectFinalizeSequence();
    void downloadCoordinator_busyCoordinator_rejectsSecondBatch();
    void downloadCoordinator_duplicateJobs_processesEachSelectionIndependently();
    void downloadCoordinator_secondBatchWhileActive_emitsBusyAndDoesNotStart();
    void downloadCoordinator_videoSpecificFailure_advancesToNextJob();
    void downloadCoordinator_sharedEnvironmentFailure_stopsBatch();
    void downloadCoordinator_apiServiceResolveSuccess_startsDownloadFromAsyncSignals();
    void downloadCoordinator_apiServiceResolveFailure_isVideoSpecificAndAdvances();
    void downloadCoordinator_apiServiceMalformedInfoResponse_isValidationFailureAndAdvances();
    void downloadCoordinator_cancelCurrentBeforeFirstStart_cancelsQueuedJobAndContinuesBatch();
    void downloadCoordinator_cancelCurrentWhileResolving_apiRequestAbortsAndNoDownloadStarts();
    void downloadCoordinator_cancelCurrentWhileConcatenating_cancelsJobAndDoesNotStartLaterStage();
    void downloadCoordinator_cancelCurrentWhileDecrypting_cancelsJob();
    void downloadCoordinator_cancelCurrentWhileDirectFinalizing_cancelsJob();
    void downloadCoordinator_cancelAllWhileConcatenating_cancelsQueuedJobsAndEmitsBatchFinishedOnce();
    void downloadCoordinator_cancelAllRepeatedly_emitsBatchFinishedExactlyOnce();
    void downloadCoordinator_decryptSharedEnvironmentFailure_stopsBatch_onCboxMissing();
    void downloadCoordinator_directFinalizeSharedEnvironmentFailure_stopsBatch_onOutputUnwritable();
    void downloadCoordinator_ownedDownloadStage_recoveryFailureThenSuccess_completesJob();
    void downloadCoordinator_ownedDownloadStage_cancelDuringDownload_abortsReplyAndPreventsConcat();
    void downloadCoordinator_ownedDownloadStage_duplicateSameTitleJobs_useDistinctTaskDirectories();
    void downloadCoordinator_ownedDecryptStage_teardownWhileActive_defersWorkerDeletionUntilThreadFinishes();
    void downloadCoordinator_ownedConcatStage_mergesTaskDirectoryAndStartsDecrypt();
    void downloadCoordinator_ownedDecryptStage_completesJob();
    void downloadCoordinator_ownedDirectFinalizeStage_completes4kJob();

    void cctvVideoDownloader_openDownloadDialog_withoutSelection_showsWarningAndDoesNotStartBatch();
    void downloadProgressWindow_show_opensNonBlockingWindow();
    void downloadProgressWindow_usesChineseStringsAndLegacyShardTable();
    void downloadProgressWindow_shardUpdates_populateLegacyModelRows();
    void downloadProgressWindow_coordinatorSignals_updateDisplay();
    void downloadProgressWindow_cancelCurrent_callsCoordinatorCancel();
    void downloadProgressWindow_cancelAll_callsCoordinatorCancelAll();
    void downloadProgressWindow_batchFinished_disablesButtons();
    void downloadProgressWindow_batchSummary_displaysMixedOutcomes();
    void downloadProgressWindow_closeWhileActive_followsConfirmationDecisionPath();
    void downloadCoordinator_eventLoopRemainsResponsiveDuringFakeLongBatch();
    void cctvVideoDownloader_openDownloadDialog_usesCoordinatorOnly();

    void apiservice_parseJsonObject_returnsEmptyOnInvalidJson();
    void apiservice_parseJsonArray_missingObjectOrArrayKey_returnsEmptyArray();
    void apiservice_processMonthData_skipsItemsWithoutGuidOrTitle();
    void apiservice_processMonthData_marksHighlightItems();
    void apiservice_processTopicVideoData_marksFragments();
    void apiservice_parseM3U8QualityUrls_and_selectQuality_chooseHighestForZero();
    void apiservice_getPlayColumnInfo_usesGuidFallbackForCctv4k();
    void apiservice_getVideoList_usesCctv4kGuidFallback();
    void apiservice_startGetPlayColumnInfo_asyncSuccess_emitsResolvedData();
    void apiservice_startGetBrowseVideoList_asyncSuccess_preservesHighlightAndFragmentBrowseSemantics();
    void apiservice_startGetImage_asyncSuccess_emitsLoadedImage();
    void apiservice_startGetEncryptM3U8Urls_asyncSuccess_emitsUrlsAnd4KFlag();
    void apiservice_startGetEncryptM3U8Urls_asyncFailure_emitsExactlyOnce();
    void apiservice_cancelGetEncryptM3U8Urls_abortsPendingReplyAndSuppressesSuccess();
    void apiservice_getEncryptM3U8Urls_cctv4kUses4000Playlist();
    void apiservice_buildVideoApiUrl_buildsExpectedQuery();
    void apiservice_buildAlbumVideoListUrl_buildsHighlightQuery();
    void apiservice_buildTopicVideoListUrl_buildsFragmentQuery();
    void apiservice_buildTsUrlsFromPlaylistData_returnsExpectedAbsoluteUrls();
    void apiservice_sendNetworkRequest_fakeSuccess_returnsDeterministicBody();
    void apiservice_sendNetworkRequest_fakeError_returnsEmptyData();
    void apiservice_getTsFileList_returnsExpectedUrlsFromSyntheticData();

    void fakeNetworkReply_success_emitsReadyReadProgressAndFinished();
    void fakeNetworkReply_abort_marksCancelledAndFinishes();
    void fakeNetworkAccessManager_queueErrorAndUnexpectedRequestFailDeterministically();
    void fakeNetworkAccessManager_delayedFinish_waitsUntilQueuedCompletion();

    // ── Import dialog tests ───────────────────────────────
    void importDialog_successPath_persistsProgramme();
    void importDialog_duplicateImport_doesNotAddExtraEntry();
    void importDialog_failurePath_resetsBusyState();
    void importDialog_staleRequestIdIgnored_resolveDoesNotPersist();
    void importDialog_staleRequestIdIgnored_failureDoesNotResetBusy();
    void importDialog_asyncClose_closesOnlyAfterPersistence();

private:
    void initializeSettingsSandbox();

    QString m_originalCurrentPath;
    std::unique_ptr<QTemporaryDir> m_tempDir;
};

void CoreRegressionTests::init()
{
    qRegisterMetaType<DownloadErrorCategory>("DownloadErrorCategory");
    qRegisterMetaType<DownloadJob>("DownloadJob");
    qRegisterMetaType<QMap<int, VideoItem>>("QMap<int, VideoItem>");
    m_originalCurrentPath = QDir::currentPath();
    m_tempDir = std::make_unique<QTemporaryDir>();
    QVERIFY2(m_tempDir->isValid(), "Temporary directory must be valid");
    QVERIFY(QDir::setCurrent(m_tempDir->path()));
    g_settings.reset();
}

void CoreRegressionTests::cleanup()
{
    APIService::instance().cancelGetEncryptM3U8Urls();
    APIServiceTestAdapter::clearTestNetworkAccessManager(APIService::instance());
    g_settings.reset();
    QVERIFY(QDir::setCurrent(m_originalCurrentPath));
    m_tempDir.reset();
}

void CoreRegressionTests::initializeSettingsSandbox()
{
    initGlobalSettings();
    QVERIFY(QFileInfo::exists(QDir(m_tempDir->path()).filePath("config/config.ini")));
    QVERIFY(g_settings != nullptr);
}

void CoreRegressionTests::initGlobalSettings_createsDefaults()
{
    initializeSettingsSandbox();

    const auto [dateBeg, dateEnd] = readDisplayMinAndMax();

    QCOMPARE(readSavePath(), QString("C:\\Video"));
    QCOMPARE(readThreadNum(), 1);
    QCOMPARE(readTranscode(), true);
    QCOMPARE(readQuality(), QString("1"));
    QCOMPARE(readLogLevel(), 1);
    QCOMPARE(readShowHighlights(), false);
    QCOMPARE(dateBeg, QDate::currentDate().toString("yyyyMM"));
    QCOMPARE(dateEnd, QDate::currentDate().addMonths(-1).toString("yyyyMM"));
}

void CoreRegressionTests::setting_saveSettings_roundTripsValuesFromWidgetsToDisk()
{
    initializeSettingsSandbox();

    Setting setting(nullptr);

    auto* savePathEdit = setting.findChild<QLineEdit*>("lineEdit_file_save_path");
    auto* threadSpin = setting.findChild<QSpinBox*>("spinBox_thread");
    auto* dateBegEdit = setting.findChild<QDateEdit*>("dateEdit_1");
    auto* dateEndEdit = setting.findChild<QDateEdit*>("dateEdit_2");
    auto* qualityCombo = setting.findChild<QComboBox*>("comboBox_quality");
    auto* logCombo = setting.findChild<QComboBox*>("comboBox_log");
    auto* highlightsCheck = setting.findChild<QCheckBox*>("checkBox_highlights");
    auto* tsRadio = setting.findChild<QRadioButton*>("radioButton_ts");
    auto* mp4Radio = setting.findChild<QRadioButton*>("radioButton_mp4");

    QVERIFY(savePathEdit != nullptr);
    QVERIFY(threadSpin != nullptr);
    QVERIFY(dateBegEdit != nullptr);
    QVERIFY(dateEndEdit != nullptr);
    QVERIFY(qualityCombo != nullptr);
    QVERIFY(logCombo != nullptr);
    QVERIFY(highlightsCheck != nullptr);
    QVERIFY(tsRadio != nullptr);
    QVERIFY(mp4Radio != nullptr);

    const QString expectedSavePath = QDir(m_tempDir->path()).filePath("custom_path");
    const int expectedThreadNum = 6;
    const QDate expectedDateBeg = QDate::currentDate().addMonths(-2);
    const QDate expectedDateEnd = QDate::currentDate().addMonths(-1);
    const int expectedQuality = 2;
    const int expectedLogLevel = 0;

    savePathEdit->setText(expectedSavePath);
    threadSpin->setValue(expectedThreadNum);
    dateBegEdit->setDate(expectedDateBeg);
    dateEndEdit->setDate(expectedDateEnd);
    qualityCombo->setCurrentIndex(expectedQuality);
    logCombo->setCurrentIndex(expectedLogLevel);
    highlightsCheck->setChecked(true);
    tsRadio->setChecked(true);

    setting.saveSettings();

    const auto [dateBeg, dateEnd] = readDisplayMinAndMax();

    QCOMPARE(readSavePath(), expectedSavePath);
    QCOMPARE(readThreadNum(), expectedThreadNum);
    QCOMPARE(readTranscode(), false);
    QCOMPARE(readQuality(), QString::number(expectedQuality));
    QCOMPARE(readLogLevel(), expectedLogLevel);
    QCOMPARE(readShowHighlights(), true);
    QCOMPARE(dateBeg, expectedDateBeg.toString("yyyyMM"));
    QCOMPARE(dateEnd, expectedDateEnd.toString("yyyyMM"));
}

void CoreRegressionTests::setting_smoke_persistsSettingsAcrossFreshSession()
{
    initializeSettingsSandbox();

    const QString expectedSavePath = QDir(m_tempDir->path()).filePath("smoke_custom_path");
    const int expectedThreadNum = 7;
    const QDate expectedDateBeg = QDate::currentDate().addMonths(-3);
    const QDate expectedDateEnd = QDate::currentDate().addMonths(-1);
    const int expectedQuality = 2;
    const int expectedLogLevel = 2;

    {
        Setting setting(nullptr);

        auto* savePathEdit = setting.findChild<QLineEdit*>("lineEdit_file_save_path");
        auto* threadSpin = setting.findChild<QSpinBox*>("spinBox_thread");
        auto* dateBegEdit = setting.findChild<QDateEdit*>("dateEdit_1");
        auto* dateEndEdit = setting.findChild<QDateEdit*>("dateEdit_2");
        auto* qualityCombo = setting.findChild<QComboBox*>("comboBox_quality");
        auto* logCombo = setting.findChild<QComboBox*>("comboBox_log");
        auto* highlightsCheck = setting.findChild<QCheckBox*>("checkBox_highlights");
        auto* tsRadio = setting.findChild<QRadioButton*>("radioButton_ts");
        auto* mp4Radio = setting.findChild<QRadioButton*>("radioButton_mp4");

        QVERIFY(savePathEdit != nullptr);
        QVERIFY(threadSpin != nullptr);
        QVERIFY(dateBegEdit != nullptr);
        QVERIFY(dateEndEdit != nullptr);
        QVERIFY(qualityCombo != nullptr);
        QVERIFY(logCombo != nullptr);
        QVERIFY(highlightsCheck != nullptr);
        QVERIFY(tsRadio != nullptr);
        QVERIFY(mp4Radio != nullptr);

        savePathEdit->setText(expectedSavePath);
        threadSpin->setValue(expectedThreadNum);
        dateBegEdit->setDate(expectedDateBeg);
        dateEndEdit->setDate(expectedDateEnd);
        qualityCombo->setCurrentIndex(expectedQuality);
        logCombo->setCurrentIndex(expectedLogLevel);
        highlightsCheck->setChecked(true);
        mp4Radio->setChecked(true);

        setting.saveSettings();
    }

    g_settings.reset();
    initGlobalSettings();

    const auto [dateBeg, dateEnd] = readDisplayMinAndMax();

    QCOMPARE(readSavePath(), expectedSavePath);
    QCOMPARE(readThreadNum(), expectedThreadNum);
    QCOMPARE(readTranscode(), true);
    QCOMPARE(readQuality(), QString::number(expectedQuality));
    QCOMPARE(readLogLevel(), expectedLogLevel);
    QCOMPARE(readShowHighlights(), true);
    QCOMPARE(dateBeg, expectedDateBeg.toString("yyyyMM"));
    QCOMPARE(dateEnd, expectedDateEnd.toString("yyyyMM"));
}

void CoreRegressionTests::downloadTask_cancelBeforeRun_emitsCancelledSignal()
{
    DownloadTask task("https://example.com/video.mp4", QDir(m_tempDir->path()).filePath("download_cancel"), QString("udata"));
    task.cancel();

    QSignalSpy spy(&task, &DownloadTask::downloadFinished);
    task.run();

    QCOMPARE(spy.count(), 1);

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QString("Cancelled"));
    QCOMPARE(arguments.at(2).toString(), QString("udata"));
}

void CoreRegressionTests::downloadTask_fileOpenFailure_emitsCannotOpenFileSignal()
{
    const auto blockedFilePath = QDir(m_tempDir->path()).filePath("download_open_fail");
    QFile blockedFile(blockedFilePath);
    QVERIFY(blockedFile.open(QIODevice::WriteOnly));
    blockedFile.close();

    DownloadTask task("https://example.com/video.mp4", blockedFilePath, QString("openfail"));

    QSignalSpy spy(&task, &DownloadTask::downloadFinished);
    task.run();

    QCOMPARE(spy.count(), 1);

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QString("Cannot open file"));
    QCOMPARE(arguments.at(2).toString(), QString("openfail"));
}

void CoreRegressionTests::downloadTask_fakeNetwork_success_writesExactBytes()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("download_fake_success");
    const QString userData = QStringLiteral("success-user");
    const QUrl url(QStringLiteral("https://fake.test/files/video-success.bin"));
    const QByteArray expectedBody("exact fake download payload\nwith second line");
    const QString expectedFilePath = QDir(downloadDir).filePath(QStringLiteral("video-success.bin"));

    manager.queueSuccess(url, expectedBody);

    DownloadTask task(url.toString(), downloadDir, userData);
    DownloadTaskTestAdapter::setTestNetworkAccessManager(task, &manager);

    QSignalSpy spy(&task, &DownloadTask::downloadFinished);
    task.run();

    QCOMPARE(spy.count(), 1);
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
    QCOMPARE(arguments.at(1).toString(), expectedFilePath);
    QCOMPARE(arguments.at(2).toString(), userData);
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
    QCOMPARE(manager.requestedUrls().constFirst(), url);

    QFile outputFile(expectedFilePath);
    QVERIFY(outputFile.open(QIODevice::ReadOnly));
    QCOMPARE(outputFile.readAll(), expectedBody);
}

void CoreRegressionTests::downloadTask_fakeNetwork_error_emitsFailureOnce()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("download_fake_error");
    const QString userData = QStringLiteral("error-user");
    const QUrl url(QStringLiteral("https://fake.test/files/video-error.bin"));
    const QString expectedError = QStringLiteral("deterministic fake download error");

    manager.queueError(url, QNetworkReply::ConnectionRefusedError, expectedError);

    DownloadTask task(url.toString(), downloadDir, userData);
    DownloadTaskTestAdapter::setTestNetworkAccessManager(task, &manager);

    QSignalSpy spy(&task, &DownloadTask::downloadFinished);
    task.run();

    QCOMPARE(spy.count(), 1);
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), expectedError);
    QCOMPARE(arguments.at(2).toString(), userData);
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
    QCOMPARE(manager.requestedUrls().constFirst(), url);
}

void CoreRegressionTests::downloadTask_defaultNetworkError_noRetry_preservesLegacyErrorString()
{
    DownloadTask defaultsTask("https://example.com/defaults.mp4", QDir(m_tempDir->path()).filePath("download_defaults"), QString("defaults-user"));
    QCOMPARE(DownloadTaskTestAdapter::timeoutMs(defaultsTask), 0);
    QCOMPARE(DownloadTaskTestAdapter::maxAttempts(defaultsTask), 1);
    QCOMPARE(DownloadTaskTestAdapter::retryDelayMs(defaultsTask), 0);

    defaultsTask.setTimeoutMs(-25);
    defaultsTask.setMaxAttempts(0);
    defaultsTask.setRetryDelayMs(-10);
    QCOMPARE(DownloadTaskTestAdapter::timeoutMs(defaultsTask), 0);
    QCOMPARE(DownloadTaskTestAdapter::maxAttempts(defaultsTask), 1);
    QCOMPARE(DownloadTaskTestAdapter::retryDelayMs(defaultsTask), 0);

    defaultsTask.setTimeoutMs(2500);
    defaultsTask.setMaxAttempts(3);
    defaultsTask.setRetryDelayMs(75);
    QCOMPARE(DownloadTaskTestAdapter::timeoutMs(defaultsTask), 2500);
    QCOMPARE(DownloadTaskTestAdapter::maxAttempts(defaultsTask), 3);
    QCOMPARE(DownloadTaskTestAdapter::retryDelayMs(defaultsTask), 75);

    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("download_fake_error_legacy_default");
    const QString userData = QStringLiteral("legacy-default-user");
    const QUrl url(QStringLiteral("https://fake.test/files/video-error-default.bin"));
    const QString expectedError = QStringLiteral("deterministic legacy default fake error");

    manager.queueError(url, QNetworkReply::ConnectionRefusedError, expectedError);

    DownloadTask task(url.toString(), downloadDir, userData);
    DownloadTaskTestAdapter::setTestNetworkAccessManager(task, &manager);

    QSignalSpy spy(&task, &DownloadTask::downloadFinished);
    task.run();

    QCOMPARE(spy.count(), 1);
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), expectedError);
    QCOMPARE(arguments.at(2).toString(), userData);
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
    QCOMPARE(manager.requestedUrls().constFirst(), url);
}

void CoreRegressionTests::downloadTask_cancelWhileReplyPending_emitsSingleCancelledCompletion()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("download_fake_cancel_pending");
    const QString userData = QStringLiteral("cancel-user");
    const QUrl url(QStringLiteral("https://fake.test/files/video-cancel.bin"));

    manager.queueSuccess(url, QByteArray(), 1000);

    DownloadTask task(url.toString(), downloadDir, userData);
    DownloadTaskTestAdapter::setTestNetworkAccessManager(task, &manager);

    QSignalSpy spy(&task, &DownloadTask::downloadFinished);
    QTimer::singleShot(10, &task, &DownloadTask::cancel);
    QElapsedTimer elapsed;
    elapsed.start();
    task.run();

    QCOMPARE(spy.count(), 1);
    QVERIFY2(elapsed.elapsed() < 300, "Cancelled fake reply should unblock promptly instead of waiting for the original delay");
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(2).toString(), userData);
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
    QCOMPARE(manager.requestedUrls().constFirst(), url);
}

void CoreRegressionTests::downloadTask_delayedFakeReply_completesWithinBoundedTime()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("download_fake_delayed");
    const QString userData = QStringLiteral("delayed-user");
    const QUrl url(QStringLiteral("https://fake.test/files/video-delayed.bin"));
    const QByteArray expectedBody("delayed fake body");

    manager.queueSuccess(url, expectedBody, 40);

    DownloadTask task(url.toString(), downloadDir, userData);
    DownloadTaskTestAdapter::setTestNetworkAccessManager(task, &manager);

    QSignalSpy spy(&task, &DownloadTask::downloadFinished);
    QElapsedTimer elapsed;
    elapsed.start();
    task.run();

    QCOMPARE(spy.count(), 1);
    QVERIFY2(elapsed.elapsed() < 500, "Delayed fake reply should complete within bounded test time");
    QVERIFY(elapsed.elapsed() >= 30);

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
    QCOMPARE(arguments.at(2).toString(), userData);
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
    QCOMPARE(manager.requestedUrls().constFirst(), url);
}

void CoreRegressionTests::downloadTask_timeoutWhileReplyStalls_emitsStructuredTimeoutOnce()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("download_fake_timeout");
    const QString userData = QStringLiteral("timeout-user");
    const QUrl url(QStringLiteral("https://fake.test/files/video-timeout.bin"));

    manager.queueSuccess(url, QByteArray(), 1000);

    DownloadTask task(url.toString(), downloadDir, userData);
    task.setTimeoutMs(20);
    DownloadTaskTestAdapter::setTestNetworkAccessManager(task, &manager);

    QSignalSpy spy(&task, &DownloadTask::downloadFinished);
    QElapsedTimer elapsed;
    elapsed.start();
    task.run();

    QCOMPARE(spy.count(), 1);
    QVERIFY2(elapsed.elapsed() < 300, "Inactivity timeout should abort a stalled fake reply promptly");
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QStringLiteral("Download failed [code=timeout; attempts=1/1]: Timed out after 20 ms"));
    QCOMPARE(arguments.at(2).toString(), userData);
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
    QCOMPARE(manager.requestedUrls().constFirst(), url);

    auto* reply = manager.lastReply();
    QVERIFY(reply != nullptr);
    QVERIFY(reply->wasAborted());
}

void CoreRegressionTests::downloadTask_completedPayloadBeforeDelayedFinish_succeedsWithoutTimeout()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("download_fake_delayed_finish_after_payload");
    const QString userData = QStringLiteral("payload-before-finish-user");
    const QUrl url(QStringLiteral("https://fake.test/files/video-delayed-finish.bin"));
    const QByteArray expectedBody("payload arrives before delayed finished");
    const QString expectedFilePath = QDir(downloadDir).filePath(QStringLiteral("video-delayed-finish.bin"));

    manager.queueSuccess(url, expectedBody, 80);

    DownloadTask task(url.toString(), downloadDir, userData);
    task.setTimeoutMs(20);
    DownloadTaskTestAdapter::setTestNetworkAccessManager(task, &manager);

    QSignalSpy spy(&task, &DownloadTask::downloadFinished);
    QElapsedTimer elapsed;
    elapsed.start();
    task.run();

    QCOMPARE(spy.count(), 1);
    QVERIFY2(elapsed.elapsed() >= 60, "Task should wait for the delayed finished signal after payload completion");
    QVERIFY2(elapsed.elapsed() < 300, "Delayed finished handling should still complete within bounded time");

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
    QCOMPARE(arguments.at(1).toString(), expectedFilePath);
    QCOMPARE(arguments.at(2).toString(), userData);
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);

    auto* reply = manager.lastReply();
    QVERIFY(reply != nullptr);
    QVERIFY(!reply->wasAborted());

    QFile outputFile(expectedFilePath);
    QVERIFY(outputFile.open(QIODevice::ReadOnly));
    QCOMPARE(outputFile.readAll(), expectedBody);
}

void CoreRegressionTests::downloadTask_cancelWhileReplyPending_withTimeoutEnabled_preservesCancellation()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("download_fake_cancel_timeout_enabled");
    const QString userData = QStringLiteral("cancel-timeout-user");
    const QUrl url(QStringLiteral("https://fake.test/files/video-cancel-timeout.bin"));

    manager.queueSuccess(url, QByteArray("cancel me"), 1000);

    DownloadTask task(url.toString(), downloadDir, userData);
    task.setTimeoutMs(200);
    DownloadTaskTestAdapter::setTestNetworkAccessManager(task, &manager);

    QSignalSpy spy(&task, &DownloadTask::downloadFinished);
    QTimer::singleShot(10, &task, &DownloadTask::cancel);
    QElapsedTimer elapsed;
    elapsed.start();
    task.run();

    QCOMPARE(spy.count(), 1);
    QVERIFY2(elapsed.elapsed() < 300, "Cancellation should still win promptly when timeout is enabled");
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QStringLiteral("Operation canceled"));
    QCOMPARE(arguments.at(2).toString(), userData);
    QVERIFY(!arguments.at(1).toString().contains(QStringLiteral("code=timeout")));
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
    QCOMPARE(manager.requestedUrls().constFirst(), url);
}

void CoreRegressionTests::downloadTask_retryOnNetworkError_thenSucceeds_writesOnlyFinalBytes()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("download_fake_retry_success");
    const QString userData = QStringLiteral("retry-success-user");
    const QUrl url(QStringLiteral("https://fake.test/files/video-retry-success.bin"));
    const QString expectedFilePath = QDir(downloadDir).filePath(QStringLiteral("video-retry-success.bin"));
    const QString firstError = QStringLiteral("first deterministic fake error");
    const QByteArray expectedBody("final success bytes only");

    manager.queueError(url, QNetworkReply::ConnectionRefusedError, firstError);
    manager.queueSuccess(url, expectedBody);

    DownloadTask task(url.toString(), downloadDir, userData);
    task.setMaxAttempts(2);
    task.setRetryDelayMs(5);
    DownloadTaskTestAdapter::setTestNetworkAccessManager(task, &manager);

    QSignalSpy spy(&task, &DownloadTask::downloadFinished);
    task.run();

    QCOMPARE(spy.count(), 1);
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
    QCOMPARE(arguments.at(1).toString(), expectedFilePath);
    QCOMPARE(arguments.at(2).toString(), userData);
    QCOMPARE(manager.requestCount(), 2);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
    QCOMPARE(manager.requestedUrls().size(), 2);
    QCOMPARE(manager.requestedUrls().at(0), url);
    QCOMPARE(manager.requestedUrls().at(1), url);

    QFile outputFile(expectedFilePath);
    QVERIFY(outputFile.open(QIODevice::ReadOnly));
    QCOMPARE(outputFile.readAll(), expectedBody);
}

void CoreRegressionTests::downloadTask_retryOnNetworkError_exhausted_emitsStructuredFailure()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("download_fake_retry_network_exhausted");
    const QString userData = QStringLiteral("retry-network-exhausted-user");
    const QUrl url(QStringLiteral("https://fake.test/files/video-retry-network-exhausted.bin"));

    manager.queueError(url, QNetworkReply::ConnectionRefusedError, QStringLiteral("first deterministic fake error"));
    manager.queueError(url, QNetworkReply::ConnectionRefusedError, QStringLiteral("second deterministic fake error"));
    manager.queueError(url, QNetworkReply::ConnectionRefusedError, QStringLiteral("third deterministic fake error"));

    DownloadTask task(url.toString(), downloadDir, userData);
    task.setMaxAttempts(3);
    task.setRetryDelayMs(5);
    DownloadTaskTestAdapter::setTestNetworkAccessManager(task, &manager);

    QSignalSpy spy(&task, &DownloadTask::downloadFinished);
    task.run();

    QCOMPARE(spy.count(), 1);
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QStringLiteral("Download failed [code=network_error; attempts=3/3]: third deterministic fake error"));
    QCOMPARE(arguments.at(2).toString(), userData);
    QCOMPARE(manager.requestCount(), 3);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::downloadTask_retryOnTimeout_exhausted_emitsStructuredTimeoutFailure()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("download_fake_retry_timeout_exhausted");
    const QString userData = QStringLiteral("retry-timeout-exhausted-user");
    const QUrl url(QStringLiteral("https://fake.test/files/video-retry-timeout-exhausted.bin"));

    manager.queueSuccess(url, QByteArray(), 1000);
    manager.queueSuccess(url, QByteArray(), 1000);

    DownloadTask task(url.toString(), downloadDir, userData);
    task.setTimeoutMs(20);
    task.setMaxAttempts(2);
    task.setRetryDelayMs(5);
    DownloadTaskTestAdapter::setTestNetworkAccessManager(task, &manager);

    QSignalSpy spy(&task, &DownloadTask::downloadFinished);
    QElapsedTimer elapsed;
    elapsed.start();
    task.run();

    QCOMPARE(spy.count(), 1);
    QVERIFY2(elapsed.elapsed() < 400, "Retry timeout exhaustion should abort promptly on each stalled attempt");
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QStringLiteral("Download failed [code=timeout; attempts=2/2]: Timed out after 20 ms"));
    QCOMPARE(arguments.at(2).toString(), userData);
    QCOMPARE(manager.requestCount(), 2);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::downloadTask_cancelDuringRetryDelay_doesNotIssueSecondRequest()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("download_fake_retry_cancel_during_delay");
    const QString userData = QStringLiteral("retry-cancel-user");
    const QUrl url(QStringLiteral("https://fake.test/files/video-retry-cancel.bin"));

    manager.queueError(url, QNetworkReply::ConnectionRefusedError, QStringLiteral("first deterministic fake error"));

    DownloadTask task(url.toString(), downloadDir, userData);
    task.setMaxAttempts(3);
    task.setRetryDelayMs(200);
    DownloadTaskTestAdapter::setTestNetworkAccessManager(task, &manager);

    QSignalSpy spy(&task, &DownloadTask::downloadFinished);
    QTimer::singleShot(10, &task, &DownloadTask::cancel);
    QElapsedTimer elapsed;
    elapsed.start();
    task.run();

    QCOMPARE(spy.count(), 1);
    QVERIFY2(elapsed.elapsed() < 150, "Cancellation during retry delay should stay event-driven and unblock promptly");
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QStringLiteral("Cancelled"));
    QCOMPARE(arguments.at(2).toString(), userData);
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::downloadTask_integrity_success_publishesFinalTs()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("integrity_success");
    const QString userData = QStringLiteral("integrity-success-user");
    const QUrl url(QStringLiteral("https://fake.test/integrity/success.bin"));
    const QByteArray expectedBody("sample ts content for integrity test");
    const QString expectedFilePath = QDir(downloadDir).filePath("success.bin");
    const QString partPath = expectedFilePath + QStringLiteral(".part");

    manager.queueSuccess(url, expectedBody);

    DownloadTask task(url.toString(), downloadDir, userData);
    DownloadTaskTestAdapter::setTestNetworkAccessManager(task, &manager);

    QSignalSpy spy(&task, &DownloadTask::downloadFinished);
    task.run();

    QCOMPARE(spy.count(), 1);
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
    QCOMPARE(arguments.at(1).toString(), expectedFilePath);
    QCOMPARE(arguments.at(2).toString(), userData);

    QVERIFY(QFileInfo::exists(expectedFilePath));
    QFile outputFile(expectedFilePath);
    QVERIFY(outputFile.open(QIODevice::ReadOnly));
    QCOMPARE(outputFile.readAll(), expectedBody);
    QVERIFY(!QFileInfo::exists(partPath));

    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::downloadTask_integrity_publishFailure_preservesExistingFinalTs()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("integrity_publish_failure");
    const QString userData = QStringLiteral("integrity-publish-failure-user");
    const QUrl url(QStringLiteral("https://fake.test/integrity/publish-failure.bin"));
    const QString expectedFilePath = QDir(downloadDir).filePath("publish-failure.bin");
    const QString partPath = expectedFilePath + QStringLiteral(".part");
    const QByteArray preExistingBody("previous good ts payload");
    const QByteArray newBody("new payload that should fail to publish");

    QVERIFY(QDir().mkpath(downloadDir));
    QFile existingFile(expectedFilePath);
    QVERIFY(existingFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(existingFile.write(preExistingBody), qint64(preExistingBody.size()));
    existingFile.close();

    manager.queueSuccess(url, newBody);

    setDownloadTaskTestRenameHook([expectedFilePath, partPath](const QString& from, const QString& to) {
        if (from == partPath && to == expectedFilePath) {
            return false;
        }
        return QFile::rename(from, to);
    });

    DownloadTask task(url.toString(), downloadDir, userData);
    DownloadTaskTestAdapter::setTestNetworkAccessManager(task, &manager);

    QSignalSpy spy(&task, &DownloadTask::downloadFinished);
    task.run();
    clearDownloadTaskTestRenameHook();

    QCOMPARE(spy.count(), 1);
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QVERIFY(arguments.at(1).toString().contains(QStringLiteral("rename_failed")));
    QVERIFY(arguments.at(1).toString().contains(expectedFilePath));
    QVERIFY(arguments.at(1).toString().contains(partPath));
    QCOMPARE(arguments.at(2).toString(), userData);

    QFile outputFile(expectedFilePath);
    QVERIFY(outputFile.open(QIODevice::ReadOnly));
    QCOMPARE(outputFile.readAll(), preExistingBody);
    QVERIFY(!QFileInfo::exists(partPath));

    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::downloadTask_integrity_emptyNoError_failsWithNoFiles()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("integrity_empty");
    const QString userData = QStringLiteral("integrity-empty-user");
    const QUrl url(QStringLiteral("https://fake.test/integrity/empty.bin"));
    const QString expectedFilePath = QDir(downloadDir).filePath("empty.bin");
    const QString partPath = expectedFilePath + QStringLiteral(".part");

    manager.queueSuccess(url, QByteArray());

    DownloadTask task(url.toString(), downloadDir, userData);
    DownloadTaskTestAdapter::setTestNetworkAccessManager(task, &manager);

    QSignalSpy spy(&task, &DownloadTask::downloadFinished);
    task.run();

    QCOMPARE(spy.count(), 1);
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QVERIFY(arguments.at(1).toString().contains(QStringLiteral("integrity")));
    QVERIFY(arguments.at(1).toString().contains(QStringLiteral("zero bytes")));
    QVERIFY(arguments.at(1).toString().contains(partPath));
    QCOMPARE(arguments.at(2).toString(), userData);

    QVERIFY(!QFileInfo::exists(expectedFilePath));
    QVERIFY(!QFileInfo::exists(partPath));
}

void CoreRegressionTests::downloadTask_integrity_shortWrite_failsImmediately()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("integrity_short_write");
    const QString userData = QStringLiteral("integrity-short-write-user");
    const QUrl url(QStringLiteral("https://fake.test/integrity/short-write.bin"));
    const QString expectedFilePath = QDir(downloadDir).filePath("short-write.bin");
    const QString partPath = expectedFilePath + QStringLiteral(".part");
    const QByteArray body("short write payload");

    manager.queueSuccess(url, body);

    setDownloadTaskTestFileWriteHook([](QFile& file, const QByteArray& data) {
        const qint64 partialBytes = qMax<qint64>(0, data.size() - 1);
        return file.write(data.constData(), partialBytes);
    });

    DownloadTask task(url.toString(), downloadDir, userData);
    DownloadTaskTestAdapter::setTestNetworkAccessManager(task, &manager);

    QSignalSpy spy(&task, &DownloadTask::downloadFinished);
    task.run();
    clearDownloadTaskTestFileWriteHook();

    QCOMPARE(spy.count(), 1);
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QVERIFY(arguments.at(1).toString().contains(QStringLiteral("write_failed")));
    QCOMPARE(arguments.at(2).toString(), userData);

    QVERIFY(!QFileInfo::exists(expectedFilePath));
    QVERIFY(!QFileInfo::exists(partPath));

    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::downloadTask_integrity_sizeMismatch_failsWithNoFiles()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("integrity_mismatch");
    const QString userData = QStringLiteral("integrity-mismatch-user");
    const QUrl url(QStringLiteral("https://fake.test/integrity/mismatch.bin"));
    const QString expectedFilePath = QDir(downloadDir).filePath("mismatch.bin");
    const QString partPath = expectedFilePath + QStringLiteral(".part");
    const QByteArray body("abc");

    manager.queueSuccess(url, body, 0, 100);

    DownloadTask task(url.toString(), downloadDir, userData);
    DownloadTaskTestAdapter::setTestNetworkAccessManager(task, &manager);

    QSignalSpy spy(&task, &DownloadTask::downloadFinished);
    task.run();

    QCOMPARE(spy.count(), 1);
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QVERIFY(arguments.at(1).toString().contains(QStringLiteral("integrity")));
    QVERIFY(arguments.at(1).toString().contains(QStringLiteral("Wrote 3 bytes")));
    QVERIFY(arguments.at(1).toString().contains(QStringLiteral("expected")));
    QVERIFY(arguments.at(1).toString().contains(partPath));
    QCOMPARE(arguments.at(2).toString(), userData);

    QVERIFY(!QFileInfo::exists(expectedFilePath));
    QVERIFY(!QFileInfo::exists(partPath));
}

void CoreRegressionTests::downloadTask_integrity_retryAfterPartial_hasOnlyFinalContent()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("integrity_retry_partial");
    const QString userData = QStringLiteral("integrity-retry-partial-user");
    const QUrl url(QStringLiteral("https://fake.test/integrity/retry-partial.bin"));
    const QString expectedFilePath = QDir(downloadDir).filePath("retry-partial.bin");
    const QString partPath = expectedFilePath + QStringLiteral(".part");

    manager.queueSuccess(url, QByteArray("par"), 0, 100);
    const QByteArray finalBody("real complete content from second attempt");
    manager.queueSuccess(url, finalBody);

    DownloadTask task(url.toString(), downloadDir, userData);
    task.setMaxAttempts(2);
    task.setRetryDelayMs(5);
    DownloadTaskTestAdapter::setTestNetworkAccessManager(task, &manager);

    QSignalSpy spy(&task, &DownloadTask::downloadFinished);
    task.run();

    QCOMPARE(spy.count(), 1);
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
    QCOMPARE(arguments.at(1).toString(), expectedFilePath);
    QCOMPARE(arguments.at(2).toString(), userData);

    QVERIFY2(!QFileInfo::exists(partPath), "retry must not leave .part behind");
    QVERIFY(QFileInfo::exists(expectedFilePath));
    QFile outputFile(expectedFilePath);
    QVERIFY(outputFile.open(QIODevice::ReadOnly));
    QCOMPARE(outputFile.readAll(), finalBody);

    QCOMPARE(manager.requestCount(), 2);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::downloadTask_integrity_unknownLengthNonEmpty_succeeds()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("integrity_unknown_length_success");
    const QString userData = QStringLiteral("integrity-unknown-length-user");
    const QUrl url(QStringLiteral("https://fake.test/integrity/unknown-length.bin"));
    const QByteArray expectedBody("non-empty payload with unknown total size");
    const QString expectedFilePath = QDir(downloadDir).filePath("unknown-length.bin");
    const QString partPath = expectedFilePath + QStringLiteral(".part");

    manager.queueSuccess(url, expectedBody, 0, -1);

    DownloadTask task(url.toString(), downloadDir, userData);
    DownloadTaskTestAdapter::setTestNetworkAccessManager(task, &manager);

    QSignalSpy spy(&task, &DownloadTask::downloadFinished);
    task.run();

    QCOMPARE(spy.count(), 1);
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
    QCOMPARE(arguments.at(1).toString(), expectedFilePath);
    QCOMPARE(arguments.at(2).toString(), userData);

    QVERIFY(QFileInfo::exists(expectedFilePath));
    QFile outputFile(expectedFilePath);
    QVERIFY(outputFile.open(QIODevice::ReadOnly));
    QCOMPARE(outputFile.readAll(), expectedBody);
    QVERIFY(!QFileInfo::exists(partPath));

    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::downloadTask_integrity_cancelBeforeAndDuring_leavesNoFiles()
{
    {
        const QString downloadDir = QDir(m_tempDir->path()).filePath("integrity_cancel_before");
        const QString expectedFilePath = QDir(downloadDir).filePath("cancel-before.bin");
        const QString partPath = expectedFilePath + QStringLiteral(".part");

        DownloadTask task(QStringLiteral("https://fake.test/integrity/cancel-before.bin"),
                          downloadDir, QStringLiteral("cancel-before-user"));
        task.cancel();

        QSignalSpy spy(&task, &DownloadTask::downloadFinished);
        task.run();

        QCOMPARE(spy.count(), 1);
        const auto arguments = spy.takeFirst();
        QCOMPARE(arguments.at(0).toBool(), false);
        QCOMPARE(arguments.at(1).toString(), QStringLiteral("Cancelled"));
        QCOMPARE(arguments.at(2).toString(), QStringLiteral("cancel-before-user"));

        QVERIFY(!QFileInfo::exists(partPath));
        QVERIFY(!QFileInfo::exists(expectedFilePath));
    }

    {
        FakeNetworkAccessManager manager;
        const QString downloadDir = QDir(m_tempDir->path()).filePath("integrity_cancel_during");
        const QString userData = QStringLiteral("cancel-during-user");
        const QUrl url(QStringLiteral("https://fake.test/integrity/cancel-during.bin"));
        const QString expectedFilePath = QDir(downloadDir).filePath("cancel-during.bin");
        const QString partPath = expectedFilePath + QStringLiteral(".part");

        manager.queueSuccess(url, QByteArray("content that will be aborted"), 80);

        DownloadTask task(url.toString(), downloadDir, userData);
        DownloadTaskTestAdapter::setTestNetworkAccessManager(task, &manager);

        QSignalSpy spy(&task, &DownloadTask::downloadFinished);
        QTimer::singleShot(10, &task, &DownloadTask::cancel);
        QElapsedTimer elapsed;
        elapsed.start();
        task.run();

        QCOMPARE(spy.count(), 1);
        QVERIFY2(elapsed.elapsed() < 300, "Cancel during reply must unblock promptly");
        const auto arguments = spy.takeFirst();
        QCOMPARE(arguments.at(0).toBool(), false);
        QCOMPARE(arguments.at(2).toString(), userData);

        QVERIFY2(!QFileInfo::exists(partPath), "cancel during reply must not leave .part");
        QVERIFY2(!QFileInfo::exists(expectedFilePath), "cancel during reply must not leave final .ts");

        QCOMPARE(manager.requestCount(), 1);
        QCOMPARE(manager.unexpectedRequestCount(), 0);
    }
}

void CoreRegressionTests::downloadEngine_idleConstructionDestruction_isSafe()
{
    DownloadEngine engine;

    QCOMPARE(engine.activeDownloads(), 0);
    QVERIFY(engine.maxThreadCount() >= 1);
    engine.cancelDownload(QStringLiteral("missing-user"));
    engine.cancelAll();
    QCOMPARE(engine.activeDownloads(), 0);
}

void CoreRegressionTests::downloadEngine_fakeNetwork_success_emitsDownloadAndAllFinished()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("engine_fake_success");
    const QString userData = QStringLiteral("engine-success-user");
    const QUrl url(QStringLiteral("https://fake.test/engine/video-success.bin"));
    const QByteArray expectedBody("engine success payload");
    const QString expectedFilePath = QDir(downloadDir).filePath(QStringLiteral("video-success.bin"));

    manager.queueSuccess(url, expectedBody);

    DownloadEngine engine;
    DownloadEngineTestAdapter::setTestReplyFactory(engine, [&manager](const QNetworkRequest& request) {
        return manager.createReplyForRequest(QNetworkAccessManager::GetOperation, request);
    });
    QSignalSpy finishedSpy(&engine, &DownloadEngine::downloadFinished);
    QSignalSpy allFinishedSpy(&engine, &DownloadEngine::allDownloadFinished);

    engine.download(url.toString(), downloadDir, userData);

    QVERIFY2(finishedSpy.wait(1000), "engine success should emit downloadFinished");
    QTRY_COMPARE_WITH_TIMEOUT(allFinishedSpy.count(), 1, 1000);
    engine.waitForAllFinished();

    QCOMPARE(engine.activeDownloads(), 0);
    QCOMPARE(finishedSpy.count(), 1);
    const auto arguments = finishedSpy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
    QCOMPARE(arguments.at(1).toString(), expectedFilePath);
    QCOMPARE(arguments.at(2).toString(), userData);
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
    QCOMPARE(manager.requestedUrls().constFirst(), url);

    QFile outputFile(expectedFilePath);
    QVERIFY(outputFile.open(QIODevice::ReadOnly));
    QCOMPARE(outputFile.readAll(), expectedBody);
}

void CoreRegressionTests::downloadEngine_fakeNetwork_error_emitsFailureAndAllFinished()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("engine_fake_error");
    const QString userData = QStringLiteral("engine-error-user");
    const QUrl url(QStringLiteral("https://fake.test/engine/video-error.bin"));
    const QString expectedError = QStringLiteral("engine deterministic fake error");

    manager.queueError(url, QNetworkReply::ConnectionRefusedError, expectedError);

    DownloadEngine engine;
    engine.setDefaultMaxAttempts(1);
    DownloadEngineTestAdapter::setTestReplyFactory(engine, [&manager](const QNetworkRequest& request) {
        return manager.createReplyForRequest(QNetworkAccessManager::GetOperation, request);
    });
    QSignalSpy finishedSpy(&engine, &DownloadEngine::downloadFinished);
    QSignalSpy allFinishedSpy(&engine, &DownloadEngine::allDownloadFinished);

    engine.download(url.toString(), downloadDir, userData);

    QVERIFY2(finishedSpy.wait(1000), "engine error should emit downloadFinished");
    QTRY_COMPARE_WITH_TIMEOUT(allFinishedSpy.count(), 1, 1000);
    engine.waitForAllFinished();

    QCOMPARE(engine.activeDownloads(), 0);
    QCOMPARE(finishedSpy.count(), 1);
    const auto arguments = finishedSpy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), expectedError);
    QCOMPARE(arguments.at(2).toString(), userData);
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
    QCOMPARE(manager.requestedUrls().constFirst(), url);
}

void CoreRegressionTests::downloadEngine_cancelActiveTask_emitsSingleCancelledFailureAndAllFinished()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("engine_fake_cancel");
    const QString userData = QStringLiteral("engine-cancel-user");
    const QUrl url(QStringLiteral("https://fake.test/engine/video-cancel.bin"));

    manager.queueSuccess(url, QByteArray("cancel me"), 120);

    DownloadEngine engine;
    DownloadEngineTestAdapter::setTestReplyFactory(engine, [&manager](const QNetworkRequest& request) {
        return manager.createReplyForRequest(QNetworkAccessManager::GetOperation, request);
    });
    QSignalSpy finishedSpy(&engine, &DownloadEngine::downloadFinished);
    QSignalSpy allFinishedSpy(&engine, &DownloadEngine::allDownloadFinished);

    engine.download(url.toString(), downloadDir, userData);
    QTRY_COMPARE_WITH_TIMEOUT(engine.activeDownloads(), 1, 200);
    QTimer::singleShot(10, &engine, [&, userData]() { engine.cancelDownload(userData); });

    QVERIFY2(finishedSpy.wait(1000), "engine cancel should emit downloadFinished");
    QTRY_COMPARE_WITH_TIMEOUT(allFinishedSpy.count(), 1, 1000);
    engine.waitForAllFinished();

    QCOMPARE(engine.activeDownloads(), 0);
    QCOMPARE(finishedSpy.count(), 1);
    const auto arguments = finishedSpy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(2).toString(), userData);
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
    QCOMPARE(manager.requestedUrls().constFirst(), url);
}

void CoreRegressionTests::downloadEngine_cancelWhenIdle_isSafeNoOp()
{
    DownloadEngine engine;
    QSignalSpy finishedSpy(&engine, &DownloadEngine::downloadFinished);
    QSignalSpy allFinishedSpy(&engine, &DownloadEngine::allDownloadFinished);

    engine.cancelDownload(QStringLiteral("idle-user"));
    engine.cancelAll();
    QTest::qWait(20);

    QCOMPARE(engine.activeDownloads(), 0);
    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(allFinishedSpy.count(), 0);
}

void CoreRegressionTests::downloadEngine_defaults_enableTimeoutAndRetry()
{
    DownloadEngine engine;

    QCOMPARE(DownloadEngineTestAdapter::defaultTimeoutMs(engine), 30000);
    QCOMPARE(DownloadEngineTestAdapter::defaultMaxAttempts(engine), 3);
    QCOMPARE(DownloadEngineTestAdapter::defaultRetryDelayMs(engine), 1000);
}

void CoreRegressionTests::downloadEngine_retrySettings_propagateToTask()
{
    FakeNetworkAccessManager manager;
    const QString downloadDir = QDir(m_tempDir->path()).filePath("engine_retry_propagate");
    const QString userData = QStringLiteral("engine-retry-propagate-user");
    const QUrl url(QStringLiteral("https://fake.test/engine/video-retry-propagate.bin"));
    const QString firstError = QStringLiteral("first engine retry error");
    const QByteArray expectedBody("engine retry success body");
    const QString expectedFilePath = QDir(downloadDir).filePath(QStringLiteral("video-retry-propagate.bin"));

    manager.queueError(url, QNetworkReply::ConnectionRefusedError, firstError);
    manager.queueSuccess(url, expectedBody);

    DownloadEngine engine;
    engine.setDefaultMaxAttempts(2);
    engine.setDefaultRetryDelayMs(5);
    DownloadEngineTestAdapter::setTestReplyFactory(engine, [&manager](const QNetworkRequest& request) {
        return manager.createReplyForRequest(QNetworkAccessManager::GetOperation, request);
    });
    QSignalSpy finishedSpy(&engine, &DownloadEngine::downloadFinished);
    QSignalSpy allFinishedSpy(&engine, &DownloadEngine::allDownloadFinished);

    engine.download(url.toString(), downloadDir, userData);

    QVERIFY2(finishedSpy.wait(2000), "engine retry propagate should emit downloadFinished");
    QTRY_COMPARE_WITH_TIMEOUT(allFinishedSpy.count(), 1, 2000);
    engine.waitForAllFinished();

    QCOMPARE(engine.activeDownloads(), 0);
    QCOMPARE(finishedSpy.count(), 1);
    const auto arguments = finishedSpy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
    QCOMPARE(arguments.at(1).toString(), expectedFilePath);
    QCOMPARE(arguments.at(2).toString(), userData);
    QCOMPARE(manager.requestCount(), 2);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
    QCOMPARE(manager.requestedUrls().size(), 2);
    QCOMPARE(manager.requestedUrls().at(0), url);
    QCOMPARE(manager.requestedUrls().at(1), url);

    QFile outputFile(expectedFilePath);
    QVERIFY(outputFile.open(QIODevice::ReadOnly));
    QCOMPARE(outputFile.readAll(), expectedBody);
}

void CoreRegressionTests::downloadDialog_recoveryPhase_rescuesFailedShardSerially()
{
    FakeNetworkAccessManager manager;
    const QString videoName = QStringLiteral("dialog-recovery-video");
    const QString savePath = m_tempDir->path();
    const QString shardUrl = QStringLiteral("https://fake.test/dialog/segment-0001.ts");
    const QStringList urls{ shardUrl };
    const QByteArray shardBody("dialog-recovered-segment");

    manager.queueError(QUrl(shardUrl), QNetworkReply::ConnectionRefusedError, QStringLiteral("initial recovery trigger error 1"));
    manager.queueError(QUrl(shardUrl), QNetworkReply::ConnectionRefusedError, QStringLiteral("initial recovery trigger error 2"));
    manager.queueSuccess(QUrl(shardUrl), shardBody);

    Download dialog(nullptr);
    DownloadDialogTestAdapter::setTestReplyFactory(dialog, [&manager](const QNetworkRequest& request) {
        return manager.createReplyForRequest(QNetworkAccessManager::GetOperation, request);
    });
    DownloadDialogTestAdapter::setTestDownloadPolicies(dialog,
        200,
        2,
        5,
        400,
        2,
        5);

    QSignalSpy finishedSpy(&dialog, &Download::DownloadFinished);
    dialog.transferDwonloadParams(videoName, urls, savePath, 2);

    QVERIFY2(finishedSpy.wait(3000), "dialog recovery should emit DownloadFinished after serial fallback succeeds");
    QCOMPARE(finishedSpy.count(), 1);
    const auto arguments = finishedSpy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);

    const QString downloadDir = QDir(savePath).filePath(QString(QCryptographicHash::hash(videoName.toUtf8(), QCryptographicHash::Sha256).toHex()));
    QFile recoveredFile(QDir(downloadDir).filePath(QStringLiteral("segment-0001.ts")));
    QVERIFY(recoveredFile.open(QIODevice::ReadOnly));
    QCOMPARE(recoveredFile.readAll(), shardBody);
    QCOMPARE(manager.requestCount(), 3);
    QCOMPARE(manager.unexpectedRequestCount(), 0);

    DownloadDialogTestAdapter::clearTestReplyFactory(dialog);
}

void CoreRegressionTests::downloadDialog_persistedPendingShards_resumeOnlyMissingPieces()
{
    FakeNetworkAccessManager manager;
    const QString videoName = QStringLiteral("dialog-resume-video");
    const QString savePath = m_tempDir->path();
    const QStringList urls{
        QStringLiteral("https://fake.test/dialog/segment-0001.ts"),
        QStringLiteral("https://fake.test/dialog/segment-0002.ts")
    };
    const QByteArray firstBody("already-downloaded-segment");
    const QByteArray secondBody("resumed-segment-body");
    const QString taskDir = QDir(savePath).filePath(QString(QCryptographicHash::hash(videoName.toUtf8(), QCryptographicHash::Sha256).toHex()));

    QVERIFY(QDir().mkpath(taskDir));

    QFile existingShard(QDir(taskDir).filePath(QStringLiteral("segment-0001.ts")));
    QVERIFY(existingShard.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(existingShard.write(firstBody), qint64(firstBody.size()));
    existingShard.close();

    QJsonObject stateRoot;
    stateRoot.insert("version", 1);
    stateRoot.insert("phase", "initial");
    QJsonArray urlsArray;
    urlsArray.append(urls.at(0));
    urlsArray.append(urls.at(1));
    stateRoot.insert("urls", urlsArray);
    QJsonArray pendingArray;
    pendingArray.append(2);
    stateRoot.insert("pending_indexes", pendingArray);
    QJsonArray completedArray;
    completedArray.append(1);
    stateRoot.insert("completed_indexes", completedArray);

    QFile stateFile(QDir(taskDir).filePath(QStringLiteral(".download_state.json")));
    QVERIFY(stateFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    stateFile.write(QJsonDocument(stateRoot).toJson(QJsonDocument::Indented));
    stateFile.close();

    manager.queueSuccess(QUrl(urls.at(1)), secondBody);

    Download dialog(nullptr);
    DownloadDialogTestAdapter::setTestReplyFactory(dialog, [&manager](const QNetworkRequest& request) {
        return manager.createReplyForRequest(QNetworkAccessManager::GetOperation, request);
    });
    DownloadDialogTestAdapter::setTestDownloadPolicies(dialog,
        200,
        2,
        5,
        400,
        2,
        5);

    QSignalSpy finishedSpy(&dialog, &Download::DownloadFinished);
    dialog.transferDwonloadParams(videoName, urls, savePath, 2);

    QVERIFY2(finishedSpy.wait(3000), "dialog resume should finish after downloading only pending shards");
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.takeFirst().at(0).toBool(), true);
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.requestedUrls().constFirst(), QUrl(urls.at(1)));
    QCOMPARE(manager.unexpectedRequestCount(), 0);

    QFile firstShard(QDir(taskDir).filePath(QStringLiteral("segment-0001.ts")));
    QFile secondShard(QDir(taskDir).filePath(QStringLiteral("segment-0002.ts")));
    QVERIFY(firstShard.open(QIODevice::ReadOnly));
    QVERIFY(secondShard.open(QIODevice::ReadOnly));
    QCOMPARE(firstShard.readAll(), firstBody);
    QCOMPARE(secondShard.readAll(), secondBody);
    QVERIFY(!QFileInfo::exists(QDir(taskDir).filePath(QStringLiteral(".download_state.json"))));

    DownloadDialogTestAdapter::clearTestReplyFactory(dialog);
}

void CoreRegressionTests::downloadDialog_withoutStateFile_redownloadsExistingShard()
{
    FakeNetworkAccessManager manager;
    const QString videoName = QStringLiteral("dialog-redownload-video");
    const QString savePath = m_tempDir->path();
    const QString shardUrl = QStringLiteral("https://fake.test/dialog/segment-0001.ts");
    const QStringList urls{ shardUrl };
    const QByteArray staleBody("partial-old-body");
    const QByteArray freshBody("fully-redownloaded-body");
    const QString taskDir = QDir(savePath).filePath(QString(QCryptographicHash::hash(videoName.toUtf8(), QCryptographicHash::Sha256).toHex()));

    QVERIFY(QDir().mkpath(taskDir));

    QFile existingShard(QDir(taskDir).filePath(QStringLiteral("segment-0001.ts")));
    QVERIFY(existingShard.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(existingShard.write(staleBody), qint64(staleBody.size()));
    existingShard.close();
    QVERIFY(!QFileInfo::exists(QDir(taskDir).filePath(QStringLiteral(".download_state.json"))));

    manager.queueSuccess(QUrl(shardUrl), freshBody);

    Download dialog(nullptr);
    DownloadDialogTestAdapter::setTestReplyFactory(dialog, [&manager](const QNetworkRequest& request) {
        return manager.createReplyForRequest(QNetworkAccessManager::GetOperation, request);
    });
    DownloadDialogTestAdapter::setTestDownloadPolicies(dialog,
        200,
        2,
        5,
        400,
        2,
        5);

    QSignalSpy finishedSpy(&dialog, &Download::DownloadFinished);
    dialog.transferDwonloadParams(videoName, urls, savePath, 1);

    QVERIFY2(finishedSpy.wait(3000), "dialog should redownload shard when no persisted completion state exists");
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.takeFirst().at(0).toBool(), true);
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.requestedUrls().constFirst(), QUrl(shardUrl));

    QFile shardFile(QDir(taskDir).filePath(QStringLiteral("segment-0001.ts")));
    QVERIFY(shardFile.open(QIODevice::ReadOnly));
    QCOMPARE(shardFile.readAll(), freshBody);
    QVERIFY(!QFileInfo::exists(QDir(taskDir).filePath(QStringLiteral(".download_state.json"))));

    DownloadDialogTestAdapter::clearTestReplyFactory(dialog);
}

void CoreRegressionTests::tsMerger_mergeCreatesOutputInTaskDirectory()
{
    const QString taskDir = QDir(m_tempDir->path()).filePath(QStringLiteral("ts_merger_task"));
    QVERIFY(QDir().mkpath(taskDir));

    const QString firstTsPath = QDir(taskDir).filePath(QStringLiteral("1.ts"));
    const QString secondTsPath = QDir(taskDir).filePath(QStringLiteral("2.ts"));
    const QString outputPath = QDir(taskDir).filePath(QStringLiteral("result.ts"));

    QVERIFY(createFakeTsFile(firstTsPath, 2, 0));
    QVERIFY(createFakeTsFile(secondTsPath, 2, 256));

    TSMerger merger;
    merger.reset();

    const std::vector<QString> inputFiles{ firstTsPath, secondTsPath };
    QVERIFY(merger.merge(inputFiles, outputPath));

    QFileInfo outputInfo(outputPath);
    QVERIFY(outputInfo.exists());
    QVERIFY(outputInfo.isFile());
    QVERIFY(outputInfo.size() > 0);
    QVERIFY(!QFileInfo::exists(QDir(taskDir).filePath(QStringLiteral("result.mp4"))));

    const MediaContainerValidationResult validation = MediaContainerValidator::validateFile(outputPath, MediaContainerType::MpegTs);
    QVERIFY2(validation.ok, qPrintable(validation.code + QStringLiteral(": ") + validation.message));
}

void CoreRegressionTests::decryptWorker_renameFailure_emitsRenameError()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_rename_failure");
    QVERIFY(QDir().mkpath(savePath));

    const QString name = QString("unit-video");
    worker.setParams(name, savePath);
    const QString taskHash = decryptTaskHash(name);
    const QString tempTaskPath = QDir(savePath).filePath(taskHash);
    QVERIFY(QDir().mkpath(tempTaskPath));
    QVERIFY(createFakeTsFile(QDir(tempTaskPath).filePath("result.ts"), 4, 256));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString cboxPath = QDir(tempTaskPath).filePath("input.cbox");
    QVERIFY(QDir().mkpath(cboxPath));

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("重命名TS->CBOX失败"));
}

void CoreRegressionTests::decryptWorker_fakeProcessRunner_seamSkipsRealProcess()
{
    const QString ffmpegPath = bundledFfmpegPath();
    QVERIFY2(QFileInfo::exists(ffmpegPath), qPrintable(QStringLiteral("Bundled ffmpeg runtime missing at %1").arg(ffmpegPath)));

    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_fake_runner_success");
    QVERIFY(QDir().mkpath(savePath));
    QVERIFY(createEmptyFile(QDir(savePath).filePath("UDRM_LICENSE.v1.0")));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());

    const QString name = QStringLiteral("fake-runner-video");
    worker.setParams(name, savePath);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString taskHash = decryptTaskHash(name);
    const QString tempTaskPath = QDir(savePath).filePath(taskHash);
    QVERIFY(QDir().mkpath(tempTaskPath));
    QVERIFY(createFakeTsFile(QDir(tempTaskPath).filePath("result.ts"), 4, 256));

    bool runnerCalled = false;
    DecryptProcessRequest capturedRequest;
    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [&](const DecryptProcessRequest& request) -> DecryptProcessResult {
        runnerCalled = true;
        capturedRequest = request;

        createFileWithContents(request.arguments.at(1), createRemuxableTsFixtureBytes());

        DecryptProcessResult result;
        result.started = true;
        result.exitCode = 0;
        result.exitStatus = QProcess::NormalExit;
        result.stdoutText = QStringLiteral("synthetic stdout");
        result.stderrText = QStringLiteral("synthetic stderr");
        return result;
    });

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    QVERIFY(runnerCalled);
    QCOMPARE(capturedRequest.arguments.size(), 2);
    QCOMPARE(capturedRequest.arguments.at(0), QDir(tempTaskPath).filePath("input.cbox"));
    QCOMPARE(capturedRequest.arguments.at(1), QDir(tempTaskPath).filePath("result.ts"));
    QVERIFY(QFileInfo::exists(QDir(savePath).filePath("fake-runner-video.mp4")));
    QVERIFY(MediaContainerValidator::validateFile(QDir(savePath).filePath("fake-runner-video.mp4"), MediaContainerType::Mp4).ok);

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("解密完成，输出 fake-runner-video"));
}

void CoreRegressionTests::decryptWorker_cboxOutputStaysInTaskDirectoryAndPreservesSaveRootResultTs()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_staging_contract");
    QVERIFY(QDir().mkpath(savePath));

    const QString saveRootResultTsPath = QDir(savePath).filePath("result.ts");
    const QByteArray saveRootSentinel("pre-existing save-root result.ts bytes");
    QVERIFY(createFileWithContents(saveRootResultTsPath, saveRootSentinel));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());

    const QString name = QStringLiteral("staging-contract-video");
    worker.setParams(name, savePath);
    DecryptWorkerTestAdapter::setTranscodeToMp4(worker, false);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString tempTaskPath = QDir(savePath).filePath(decryptTaskHash(name));
    QVERIFY(QDir().mkpath(tempTaskPath));
    QVERIFY(createFakeTsFile(QDir(tempTaskPath).filePath("result.ts"), 4, 256));

    bool runnerCalled = false;
    DecryptProcessRequest capturedRequest;
    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [&](const DecryptProcessRequest& request) -> DecryptProcessResult {
        runnerCalled = true;
        capturedRequest = request;
        createFakeTsFile(request.arguments.at(1), 4, 512);

        DecryptProcessResult result;
        result.started = true;
        result.exitCode = 0;
        result.exitStatus = QProcess::NormalExit;
        return result;
    });

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    QVERIFY(runnerCalled);
    QCOMPARE(capturedRequest.arguments.size(), 2);
    QCOMPARE(capturedRequest.arguments.at(0), QDir(tempTaskPath).filePath("input.cbox"));
    QCOMPARE(capturedRequest.arguments.at(1), QDir(tempTaskPath).filePath("result.ts"));
    QVERIFY(QFileInfo::exists(QDir(savePath).filePath("staging-contract-video.ts")));
    QVERIFY(!QFileInfo::exists(tempTaskPath));

    QFile saveRootResultTs(saveRootResultTsPath);
    QVERIFY(saveRootResultTs.open(QIODevice::ReadOnly));
    QCOMPARE(saveRootResultTs.readAll(), saveRootSentinel);

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("解密完成，输出 staging-contract-video"));
}

void CoreRegressionTests::decryptWorker_cleanupFailure_emitsSingleFailureAfterPublish()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_cleanup_failure");
    QVERIFY(QDir().mkpath(savePath));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());

    const QString name = QStringLiteral("cleanup-failure-video");
    worker.setParams(name, savePath);
    DecryptWorkerTestAdapter::setTranscodeToMp4(worker, false);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString tempTaskPath = QDir(savePath).filePath(decryptTaskHash(name));
    QVERIFY(QDir().mkpath(tempTaskPath));
    QVERIFY(createFakeTsFile(QDir(tempTaskPath).filePath("result.ts"), 4, 256));

    std::unique_ptr<QFile> lockedCleanupFile;
    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [&](const DecryptProcessRequest& request) -> DecryptProcessResult {
        createFakeTsFile(request.arguments.at(1), 4, 768);

        lockedCleanupFile = std::make_unique<QFile>(QDir(tempTaskPath).filePath("locked-cleanup.tmp"));
        if (lockedCleanupFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            lockedCleanupFile->write("locked");
            lockedCleanupFile->flush();
        }

        DecryptProcessResult result;
        result.started = true;
        result.exitCode = 0;
        result.exitStatus = QProcess::NormalExit;
        return result;
    });

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    QVERIFY(QFileInfo::exists(QDir(savePath).filePath("cleanup-failure-video.ts")));

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("移除临时文件夹失败\n请手动清理"));

    if (lockedCleanupFile) {
        lockedCleanupFile->close();
    }
    QDir(tempTaskPath).removeRecursively();
}

void CoreRegressionTests::decryptWorker_processTimeoutNormalization_passesDefaultTimeoutToRunner()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_timeout_default");
    QVERIFY(QDir().mkpath(savePath));
    QVERIFY(createEmptyFile(QDir(savePath).filePath("UDRM_LICENSE.v1.0")));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());

    const QString name = QStringLiteral("timeout-default-video");
    worker.setParams(name, savePath);
    DecryptWorkerTestAdapter::setTranscodeToMp4(worker, false);
    DecryptWorkerTestAdapter::setProcessTimeoutMs(worker, 0);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString taskHash = decryptTaskHash(name);
    const QString tempTaskPath = QDir(savePath).filePath(taskHash);
    QVERIFY(QDir().mkpath(tempTaskPath));
    QVERIFY(createFakeTsFile(QDir(tempTaskPath).filePath("result.ts"), 4, 256));

    int seenTimeoutMs = -1;
    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [&](const DecryptProcessRequest& request) -> DecryptProcessResult {
        seenTimeoutMs = request.timeoutMs;

        createFakeTsFile(request.arguments.at(1), 4, 512);

        DecryptProcessResult result;
        result.started = true;
        result.exitCode = 0;
        result.exitStatus = QProcess::NormalExit;
        return result;
    });

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(seenTimeoutMs, 30000);
    QVERIFY(QFileInfo::exists(QDir(savePath).filePath("timeout-default-video.ts")));

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("解密完成，输出 timeout-default-video"));
}

void CoreRegressionTests::decryptWorker_testDecryptAssetsDir_usesTemporaryAssets()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_assets_dir_success");
    QVERIFY(QDir().mkpath(savePath));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());

    const QString name = QStringLiteral("assets-dir-video");
    worker.setParams(name, savePath);
    DecryptWorkerTestAdapter::setTranscodeToMp4(worker, false);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString taskHash = decryptTaskHash(name);
    const QString tempTaskPath = QDir(savePath).filePath(taskHash);
    QVERIFY(QDir().mkpath(tempTaskPath));
    QVERIFY(createFakeTsFile(QDir(tempTaskPath).filePath("result.ts"), 4, 256));

    DecryptProcessRequest capturedRequest;
    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [&](const DecryptProcessRequest& request) -> DecryptProcessResult {
        capturedRequest = request;

        createFakeTsFile(request.arguments.at(1), 4, 768);

        DecryptProcessResult result;
        result.started = true;
        result.exitCode = 0;
        result.exitStatus = QProcess::NormalExit;
        return result;
    });

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(capturedRequest.program, QDir(decryptAssetsDir.path()).filePath("cbox.exe"));
    QVERIFY(QFileInfo::exists(QDir(savePath).filePath("assets-dir-video.ts")));
    QVERIFY(!QFileInfo::exists(QDir(savePath).filePath("UDRM_LICENSE.v1.0")));

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("解密完成，输出 assets-dir-video"));
}

void CoreRegressionTests::decryptWorker_invalidParams_emitsStructuredPreflightError()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    int runnerCallCount = 0;
    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [&](const DecryptProcessRequest&) {
        ++runnerCallCount;
        return DecryptProcessResult{};
    });

    worker.setParams(QStringLiteral("   "), QStringLiteral("   "));
    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(runnerCallCount, 0);
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("解密失败 [code=invalid_params]: 解密参数无效"));
}

void CoreRegressionTests::decryptWorker_missingInput_emitsPreflightErrorBeforeMutation()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_missing_input");
    QVERIFY(QDir().mkpath(savePath));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());

    const QString name = QStringLiteral("missing-input-video");
    worker.setParams(name, savePath);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString tempTaskPath = QDir(savePath).filePath(decryptTaskHash(name));
    QVERIFY(QDir().mkpath(tempTaskPath));
    const QString cboxPath = QDir(tempTaskPath).filePath("input.cbox");

    int runnerCallCount = 0;
    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [&](const DecryptProcessRequest&) {
        ++runnerCallCount;
        return DecryptProcessResult{};
    });

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(runnerCallCount, 0);
    QVERIFY(!QFileInfo::exists(cboxPath));
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("解密失败 [code=input_missing]: result.ts 不存在"));
}

void CoreRegressionTests::decryptWorker_missingCbox_emitsPreflightErrorBeforeProcessStart()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_missing_cbox");
    QVERIFY(QDir().mkpath(savePath));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path(), false, true);

    const QString name = QStringLiteral("missing-cbox-video");
    worker.setParams(name, savePath);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString tempTaskPath = QDir(savePath).filePath(decryptTaskHash(name));
    QVERIFY(QDir().mkpath(tempTaskPath));
    QVERIFY(createFakeTsFile(QDir(tempTaskPath).filePath("result.ts"), 4, 256));

    int runnerCallCount = 0;
    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [&](const DecryptProcessRequest&) {
        ++runnerCallCount;
        return DecryptProcessResult{};
    });

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(runnerCallCount, 0);
    QVERIFY(!QFileInfo::exists(QDir(tempTaskPath).filePath("input.cbox")));
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("解密失败 [code=cbox_missing]: decrypt/cbox.exe 不存在"));
}

void CoreRegressionTests::decryptWorker_missingLicense_emitsPreflightErrorBeforeProcessStart()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_missing_license");
    QVERIFY(QDir().mkpath(savePath));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path(), true, false);

    const QString name = QString("license-video");
    worker.setParams(name, savePath);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString taskHash = decryptTaskHash(name);
    const QString tempTaskPath = QDir(savePath).filePath(taskHash);
    QVERIFY(QDir().mkpath(tempTaskPath));
    const QString mp4Path = QDir(tempTaskPath).filePath("result.ts");
    QVERIFY(createFakeTsFile(mp4Path, 4, 256));

    int runnerCallCount = 0;
    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [&](const DecryptProcessRequest&) {
        ++runnerCallCount;
        DecryptProcessResult result;
        return result;
    });

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(runnerCallCount, 0);
    QVERIFY(!QFileInfo::exists(QDir(tempTaskPath).filePath("input.cbox")));
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("解密失败 [code=license_missing]: 解密所需的许可证文件不存在"));
}

void CoreRegressionTests::decryptWorker_outputUnwritable_emitsPreflightErrorBeforeProcessStart()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_output_unwritable");
    QVERIFY(QDir().mkpath(savePath));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());

    const QString name = QStringLiteral("unwritable-output-video");
    worker.setParams(name, savePath);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString tempTaskPath = QDir(savePath).filePath(decryptTaskHash(name));
    QVERIFY(QDir().mkpath(tempTaskPath));
    QVERIFY(createFakeTsFile(QDir(tempTaskPath).filePath("result.ts"), 4, 256));

    const QString probePath = QDir(savePath).filePath(
        QStringLiteral(".decrypt_write_probe_%1.tmp").arg(QString::number(QCoreApplication::applicationPid()))
    );
    QVERIFY(QDir().mkpath(probePath));

    int runnerCallCount = 0;
    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [&](const DecryptProcessRequest&) {
        ++runnerCallCount;
        return DecryptProcessResult{};
    });

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(runnerCallCount, 0);
    QVERIFY(!QFileInfo::exists(QDir(tempTaskPath).filePath("input.cbox")));
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("解密失败 [code=output_unwritable]: 输出目录不可写"));
}

void CoreRegressionTests::decryptWorker_startFailed_emitsStructuredProcessStartError()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_start_failed");
    QVERIFY(QDir().mkpath(savePath));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());

    const QString name = QStringLiteral("start-failed-video");
    worker.setParams(name, savePath);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString tempTaskPath = QDir(savePath).filePath(decryptTaskHash(name));
    QVERIFY(QDir().mkpath(tempTaskPath));
    QVERIFY(createFakeTsFile(QDir(tempTaskPath).filePath("result.ts"), 4, 256));

    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [](const DecryptProcessRequest&) {
        DecryptProcessResult result;
        result.errorString = QStringLiteral("synthetic launch failure");
        return result;
    });

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    QVERIFY(QFileInfo::exists(QDir(tempTaskPath).filePath("result.ts")));
    QVERIFY(!QFileInfo::exists(QDir(tempTaskPath).filePath("input.cbox")));

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("解密失败 [code=start_failed]: 无法启动cbox: synthetic launch failure"));
}

void CoreRegressionTests::decryptWorker_timedOut_emitsStructuredTimeoutError()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_timeout");
    QVERIFY(QDir().mkpath(savePath));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());

    const QString name = QStringLiteral("timeout-video");
    worker.setParams(name, savePath);
    DecryptWorkerTestAdapter::setProcessTimeoutMs(worker, 50);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString tempTaskPath = QDir(savePath).filePath(decryptTaskHash(name));
    QVERIFY(QDir().mkpath(tempTaskPath));
    const QString resultMp4Path = QDir(tempTaskPath).filePath("result.ts");
    const QString inputCboxPath = QDir(tempTaskPath).filePath("input.cbox");
    QVERIFY(createFakeTsFile(resultMp4Path, 4, 256));

    int observedTimeoutMs = -1;
    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [&](const DecryptProcessRequest& request) -> DecryptProcessResult {
        observedTimeoutMs = request.timeoutMs;
        DecryptProcessResult result;
        result.started = true;
        result.timedOut = true;
        result.stdoutText = QStringLiteral("partial stdout");
        result.stderrText = QStringLiteral("partial stderr");
        return result;
    });

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(observedTimeoutMs, 50);
    QVERIFY(QFileInfo::exists(tempTaskPath));
    QVERIFY(QFileInfo::exists(resultMp4Path));
    QVERIFY(!QFileInfo::exists(inputCboxPath));
    QVERIFY(!QFileInfo::exists(QDir(savePath).filePath("UDRM_LICENSE.v1.0")));

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("解密失败 [code=timeout]: cbox 超时 50 ms"));
}

void CoreRegressionTests::decryptWorker_processFailed_prefersStderrAndCleansUpArtifacts()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_process_failed_stderr");
    QVERIFY(QDir().mkpath(savePath));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());

    const QString name = QStringLiteral("process-failed-stderr-video");
    worker.setParams(name, savePath);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString tempTaskPath = QDir(savePath).filePath(decryptTaskHash(name));
    QVERIFY(QDir().mkpath(tempTaskPath));
    QVERIFY(createFakeTsFile(QDir(tempTaskPath).filePath("result.ts"), 4, 256));

    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [&](const DecryptProcessRequest&) -> DecryptProcessResult {
        if (!createEmptyFile(QDir(savePath).filePath("output.txt"))) {
            return DecryptProcessResult{};
        }

        DecryptProcessResult result;
        result.started = true;
        result.exitCode = 23;
        result.exitStatus = QProcess::NormalExit;
        result.stdoutText = QStringLiteral("stdout diagnostic");
        result.stderrText = QStringLiteral("  stderr diagnostic  ");
        return result;
    });

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    QVERIFY(QFileInfo::exists(tempTaskPath));
    QVERIFY(QFileInfo::exists(QDir(tempTaskPath).filePath("result.ts")));
    QVERIFY(!QFileInfo::exists(QDir(tempTaskPath).filePath("input.cbox")));
    QVERIFY(!QFileInfo::exists(QDir(savePath).filePath("output.txt")));
    QVERIFY(!QFileInfo::exists(QDir(savePath).filePath("UDRM_LICENSE.v1.0")));

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("解密失败 [code=process_failed; exit_code=23]: stderr diagnostic"));
}

void CoreRegressionTests::decryptWorker_processFailed_fallsBackToStdoutDiagnostic()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_process_failed_stdout");
    QVERIFY(QDir().mkpath(savePath));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());

    const QString name = QStringLiteral("process-failed-stdout-video");
    worker.setParams(name, savePath);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString tempTaskPath = QDir(savePath).filePath(decryptTaskHash(name));
    QVERIFY(QDir().mkpath(tempTaskPath));
    QVERIFY(createFakeTsFile(QDir(tempTaskPath).filePath("result.ts"), 4, 256));

    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [](const DecryptProcessRequest&) {
        DecryptProcessResult result;
        result.started = true;
        result.exitCode = 9;
        result.exitStatus = QProcess::NormalExit;
        result.stdoutText = QStringLiteral("  stdout fallback diagnostic  ");
        result.stderrText = QStringLiteral("   ");
        return result;
    });

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("解密失败 [code=process_failed; exit_code=9]: stdout fallback diagnostic"));
}

void CoreRegressionTests::decryptWorker_processFailed_truncatesLongDiagnostic()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_process_failed_long_diagnostic");
    QVERIFY(QDir().mkpath(savePath));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());

    const QString name = QStringLiteral("process-failed-long-diagnostic-video");
    worker.setParams(name, savePath);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString tempTaskPath = QDir(savePath).filePath(decryptTaskHash(name));
    QVERIFY(QDir().mkpath(tempTaskPath));
    QVERIFY(createFakeTsFile(QDir(tempTaskPath).filePath("result.ts"), 4, 256));

    const QString longStderr(3000, QChar('x'));
    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [&](const DecryptProcessRequest&) {
        DecryptProcessResult result;
        result.started = true;
        result.exitCode = 5;
        result.exitStatus = QProcess::NormalExit;
        result.stderrText = longStderr;
        return result;
    });

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    const QString message = arguments.at(1).toString();
    const QString prefix = QString::fromUtf8("解密失败 [code=process_failed; exit_code=5]: ");
    QVERIFY(message.startsWith(prefix));
    QCOMPARE(message.size(), prefix.size() + 2048);
    QVERIFY(!message.contains(QString(2500, QChar('x'))));
}

void CoreRegressionTests::decryptWorker_processFailed_restoresTempResultMp4()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_process_failed_rollback");
    QVERIFY(QDir().mkpath(savePath));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());

    const QString name = QStringLiteral("process-failed-rollback-video");
    worker.setParams(name, savePath);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString tempTaskPath = QDir(savePath).filePath(decryptTaskHash(name));
    QVERIFY(QDir().mkpath(tempTaskPath));
    const QString resultMp4Path = QDir(tempTaskPath).filePath("result.ts");
    const QString inputCboxPath = QDir(tempTaskPath).filePath("input.cbox");
    QVERIFY(createFakeTsFile(resultMp4Path, 4, 256));

    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [](const DecryptProcessRequest&) {
        DecryptProcessResult result;
        result.started = true;
        result.exitCode = 41;
        result.exitStatus = QProcess::NormalExit;
        result.stderrText = QStringLiteral("rollback diagnostic");
        return result;
    });

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    QVERIFY(QFileInfo::exists(tempTaskPath));
    QVERIFY(QFileInfo::exists(resultMp4Path));
    QVERIFY(!QFileInfo::exists(inputCboxPath));

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("解密失败 [code=process_failed; exit_code=41]: rollback diagnostic"));
}


void CoreRegressionTests::decryptWorker_processFailed_preservesPreExistingLicense()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_process_failed_preexisting_license");
    QVERIFY(QDir().mkpath(savePath));

    const QByteArray preExistingLicenseBytes("pre-existing license bytes");
    const QString licenseTarget = QDir(savePath).filePath("UDRM_LICENSE.v1.0");
    QFile preExistingLicense(licenseTarget);
    QVERIFY(preExistingLicense.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(preExistingLicense.write(preExistingLicenseBytes), qint64(preExistingLicenseBytes.size()));
    preExistingLicense.close();

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());

    const QString name = QStringLiteral("process-failed-preexisting-license-video");
    worker.setParams(name, savePath);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString tempTaskPath = QDir(savePath).filePath(decryptTaskHash(name));
    QVERIFY(QDir().mkpath(tempTaskPath));
    const QString resultMp4Path = QDir(tempTaskPath).filePath("result.ts");
    const QString inputCboxPath = QDir(tempTaskPath).filePath("input.cbox");
    QVERIFY(createFakeTsFile(resultMp4Path, 4, 256));

    bool outputCreated = false;
    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [&](const DecryptProcessRequest&) {
        outputCreated = createEmptyFile(QDir(savePath).filePath("output.txt"));

        DecryptProcessResult result;
        result.started = true;
        result.exitCode = 13;
        result.exitStatus = QProcess::NormalExit;
        result.stderrText = QStringLiteral("preexisting license diagnostic");
        return result;
    });

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    QVERIFY(outputCreated);
    QVERIFY(QFileInfo::exists(resultMp4Path));
    QVERIFY(!QFileInfo::exists(inputCboxPath));
    QVERIFY(!QFileInfo::exists(QDir(savePath).filePath("output.txt")));
    QVERIFY(QFileInfo::exists(licenseTarget));

    QFile preservedLicense(licenseTarget);
    QVERIFY(preservedLicense.open(QIODevice::ReadOnly));
    QCOMPARE(preservedLicense.readAll(), preExistingLicenseBytes);

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("解密失败 [code=process_failed; exit_code=13]: preexisting license diagnostic"));
}


void CoreRegressionTests::decryptWorker_success_preservesPreExistingLicense()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_success_preexisting_license");
    QVERIFY(QDir().mkpath(savePath));

    const QByteArray preExistingLicenseBytes("pre-existing license bytes");
    const QString licenseTarget = QDir(savePath).filePath("UDRM_LICENSE.v1.0");
    QFile preExistingLicense(licenseTarget);
    QVERIFY(preExistingLicense.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(preExistingLicense.write(preExistingLicenseBytes), qint64(preExistingLicenseBytes.size()));
    preExistingLicense.close();

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());

    const QString name = QStringLiteral("success-preexisting-license-video");
    worker.setParams(name, savePath);
    DecryptWorkerTestAdapter::setTranscodeToMp4(worker, false);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString tempTaskPath = QDir(savePath).filePath(decryptTaskHash(name));
    QVERIFY(QDir().mkpath(tempTaskPath));
    QVERIFY(createFakeTsFile(QDir(tempTaskPath).filePath("result.ts"), 4, 256));

    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [](const DecryptProcessRequest& request) {
        createFakeTsFile(request.arguments.at(1), 4, 1024);

        DecryptProcessResult result;
        result.started = true;
        result.exitCode = 0;
        result.exitStatus = QProcess::NormalExit;
        return result;
    });

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    QVERIFY(QFileInfo::exists(QDir(savePath).filePath("success-preexisting-license-video.ts")));
    QVERIFY(QFileInfo::exists(licenseTarget));

    QFile preservedLicense(licenseTarget);
    QVERIFY(preservedLicense.open(QIODevice::ReadOnly));
    QCOMPARE(preservedLicense.readAll(), preExistingLicenseBytes);

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("解密完成，输出 success-preexisting-license-video"));
}

void CoreRegressionTests::decryptWorker_success_canKeepDecryptedTs()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_success_keep_ts");
    QVERIFY(QDir().mkpath(savePath));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());

    const QString name = QStringLiteral("keep-ts-video");
    worker.setParams(name, savePath);
    DecryptWorkerTestAdapter::setTranscodeToMp4(worker, false);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());
    QString cboxOutputFileName;

    const QString tempTaskPath = QDir(savePath).filePath(decryptTaskHash(name));
    QVERIFY(QDir().mkpath(tempTaskPath));
    QVERIFY(createFakeTsFile(QDir(tempTaskPath).filePath("result.ts"), 4, 256));

    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [&cboxOutputFileName](const DecryptProcessRequest& request) {
        cboxOutputFileName = QFileInfo(request.arguments.at(1)).fileName();
        createFakeTsFile(request.arguments.at(1), 4, 1280);

        DecryptProcessResult result;
        result.started = true;
        result.exitCode = 0;
        result.exitStatus = QProcess::NormalExit;
        return result;
    });

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(cboxOutputFileName, QString("result.ts"));
    QVERIFY(QFileInfo::exists(QDir(savePath).filePath("keep-ts-video.ts")));
    QVERIFY(!QFileInfo::exists(QDir(savePath).filePath("keep-ts-video.mp4")));
    QVERIFY(MediaContainerValidator::validateFile(QDir(savePath).filePath("keep-ts-video.ts"), MediaContainerType::MpegTs).ok);

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
}

void CoreRegressionTests::decryptWorker_invalidCboxOutput_rejectsAndDoesNotPublish()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_invalid_cbox_output");
    QVERIFY(QDir().mkpath(savePath));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());

    const QString name = QStringLiteral("invalid-cbox-output-video");
    worker.setParams(name, savePath);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString tempTaskPath = QDir(savePath).filePath(decryptTaskHash(name));
    QVERIFY(QDir().mkpath(tempTaskPath));
    const QString resultTsPath = QDir(tempTaskPath).filePath("result.ts");
    const QString inputCboxPath = QDir(tempTaskPath).filePath("input.cbox");
    const QString stagedOutputPath = QDir(savePath).filePath("result.ts");
    QVERIFY(createFakeTsFile(resultTsPath, 4, 256));

    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [](const DecryptProcessRequest& request) {
        createFileWithContents(request.arguments.at(1), createFakeMp4Bytes());

        DecryptProcessResult result;
        result.started = true;
        result.exitCode = 0;
        result.exitStatus = QProcess::NormalExit;
        return result;
    });

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    QVERIFY(QFileInfo::exists(resultTsPath));
    QVERIFY(!QFileInfo::exists(inputCboxPath));
    QVERIFY(!QFileInfo::exists(stagedOutputPath));
    QVERIFY(!QFileInfo::exists(QDir(savePath).filePath("invalid-cbox-output-video.ts")));
    QVERIFY(!QFileInfo::exists(QDir(savePath).filePath("invalid-cbox-output-video.mp4")));

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("解密失败 [code=invalid_cbox_output]: [unexpected_container] Detected media container does not match expected type"));
}

void CoreRegressionTests::decryptWorker_crashExitWithZeroExitCode_emitsProcessFailure()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_crash_exit_zero");
    QVERIFY(QDir().mkpath(savePath));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());

    const QString name = QStringLiteral("crash-exit-zero-video");
    worker.setParams(name, savePath);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString tempTaskPath = QDir(savePath).filePath(decryptTaskHash(name));
    QVERIFY(QDir().mkpath(tempTaskPath));
    const QString resultMp4Path = QDir(tempTaskPath).filePath("result.ts");
    const QString inputCboxPath = QDir(tempTaskPath).filePath("input.cbox");
    QVERIFY(createFakeTsFile(resultMp4Path, 4, 256));

    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [](const DecryptProcessRequest&) {
        DecryptProcessResult result;
        result.started = true;
        result.exitCode = 0;
        result.exitStatus = QProcess::CrashExit;
        result.stderrText = QStringLiteral("crash diagnostic");
        return result;
    });

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    QVERIFY(QFileInfo::exists(resultMp4Path));
    QVERIFY(!QFileInfo::exists(inputCboxPath));
    QVERIFY(!QFileInfo::exists(QDir(savePath).filePath("UDRM_LICENSE.v1.0")));
    QVERIFY(!QFileInfo::exists(QDir(savePath).filePath("crash-exit-zero-video.mp4")));

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("解密失败 [code=process_failed; exit_code=0]: crash diagnostic"));
}

void CoreRegressionTests::decryptWorker_cancelDuringProcess_emitsCancelledAndDoesNotPublish()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_cancel_during_process");
    QVERIFY(QDir().mkpath(savePath));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());

    const QString name = QStringLiteral("cancel-during-process-video");
    worker.setParams(name, savePath);
    DecryptWorkerTestAdapter::setTranscodeToMp4(worker, false);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString tempTaskPath = QDir(savePath).filePath(decryptTaskHash(name));
    QVERIFY(QDir().mkpath(tempTaskPath));
    const QString resultTsPath = QDir(tempTaskPath).filePath("result.ts");
    const QString inputCboxPath = QDir(tempTaskPath).filePath("input.cbox");
    QVERIFY(createFakeTsFile(resultTsPath, 4, 256));

    std::atomic_bool runnerStarted{ false };
    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [&](const DecryptProcessRequest& request) {
        runnerStarted.store(true, std::memory_order_relaxed);

        for (int i = 0; i < 200; ++i) {
            if (request.cancellationRequested && request.cancellationRequested()) {
                DecryptProcessResult result;
                result.started = true;
                result.cancelled = true;
                return result;
            }

            QThread::msleep(5);
        }

        DecryptProcessResult result;
        result.started = true;
        result.exitCode = 0;
        result.exitStatus = QProcess::NormalExit;
        return result;
    });

    QThread thread;
    worker.moveToThread(&thread);
    connect(&thread, &QThread::started, &worker, &DecryptWorker::doDecrypt);
    connect(&worker, &DecryptWorker::decryptFinished, &thread, &QThread::quit);

    thread.start();
    QTRY_VERIFY(runnerStarted.load(std::memory_order_relaxed));
    worker.cancelDecrypt();
    QVERIFY(spy.wait(2000));
    thread.wait();
    worker.moveToThread(QCoreApplication::instance()->thread());

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    QVERIFY(QFileInfo::exists(resultTsPath));
    QVERIFY(!QFileInfo::exists(inputCboxPath));
    QVERIFY(!QFileInfo::exists(QDir(savePath).filePath("cancel-during-process-video.ts")));
    QVERIFY(!QFileInfo::exists(QDir(savePath).filePath("cancel-during-process-video.mp4")));

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QStringLiteral("cancelled"));
}

void CoreRegressionTests::apiservice_parseJsonObject_returnsEmptyOnInvalidJson()
{
    APIService& apiService = APIService::instance();

    const auto result = APIServiceTestAdapter::parseJsonObject(apiService, QByteArray("not a json"), QString("manifest"));

    QVERIFY2(result.isEmpty(), "invalid JSON should return empty object");
}

void CoreRegressionTests::apiservice_parseJsonArray_missingObjectOrArrayKey_returnsEmptyArray()
{
    APIService& apiService = APIService::instance();

    const QByteArray payload = R"({"root":{"videos":[{"name":"a"}]}})";
    const auto missingArray = APIServiceTestAdapter::parseJsonArray(apiService, payload, QString("root"), QString("items"));
    QVERIFY2(missingArray.isEmpty(), "missing array key should return empty array");

    const auto missingObject = APIServiceTestAdapter::parseJsonArray(apiService, payload, QString("missing"), QString("videos"));
    QVERIFY2(missingObject.isEmpty(), "missing object key should return empty array");
}

void CoreRegressionTests::apiservice_processMonthData_skipsItemsWithoutGuidOrTitle()
{
    APIService& apiService = APIService::instance();

    QJsonArray items{
        QJsonObject{{"guid", "valid-1"}, {"title", "valid"}, {"time", "t"}},
        QJsonObject{{"guid", "invalid-missing-title"}},
        QJsonObject{{"title", "invalid-missing-guid"}},
        QJsonObject{{"guid", "valid-2"}, {"title", "also-valid"}, {"time", "t2"}}
    };

    QMap<int, VideoItem> result;
    int index = 0;

    APIServiceTestAdapter::processMonthData(apiService, items, QString("202501"), result, index);

    QCOMPARE(result.size(), 2);
    QCOMPARE(index, 2);
    QCOMPARE(result.value(0).guid, QString("valid-1"));
    QCOMPARE(result.value(1).guid, QString("valid-2"));
}

void CoreRegressionTests::apiservice_processMonthData_marksHighlightItems()
{
    APIService& apiService = APIService::instance();

    QJsonArray items{
        QJsonObject{
            {"guid", "highlight-guid"},
            {"title", "highlight-title"},
            {"time", "2025-09-28 19:57:04"},
            {"image", "https://example.test/highlight.jpg"},
            {"brief", "highlight brief"}
        }
    };

    QMap<int, VideoItem> result;
    int index = 0;

    APIServiceTestAdapter::processMonthData(apiService, items, QString("highlight"), result, index);
    QVERIFY(!result.value(0).isHighlight);

    result.clear();
    index = 0;
    APIServiceTestAdapter::processMonthData(apiService, items, QString("highlight"), result, index, true);

    QCOMPARE(result.size(), 1);
    QVERIFY(result.value(0).isHighlight);
    QCOMPARE(result.value(0).guid, QString("highlight-guid"));
}

void CoreRegressionTests::apiservice_processTopicVideoData_marksFragments()
{
    APIService& apiService = APIService::instance();

    QJsonArray items{
        QJsonObject{
            {"guid", "fragment-guid"},
            {"video_title", "fragment-title"},
            {"video_focus_date", "2022-06-24 23:29:56"},
            {"video_key_frame_url", "https://example.test/fragment.jpg"},
            {"sc", "fragment brief"}
        }
    };

    QMap<int, VideoItem> result;
    int index = 0;
    APIServiceTestAdapter::processTopicVideoData(apiService, items, result, index);

    QCOMPARE(result.size(), 1);
    QCOMPARE(index, 1);
    QCOMPARE(result.value(0).guid, QString("fragment-guid"));
    QCOMPARE(result.value(0).title, QString("fragment-title"));
    QVERIFY(result.value(0).isHighlight);
    QCOMPARE(result.value(0).listType, QString::fromUtf8("片段"));
}

void CoreRegressionTests::apiservice_parseM3U8QualityUrls_and_selectQuality_chooseHighestForZero()
{
    APIService& apiService = APIService::instance();

    const QByteArray m3u8Payload = R"(
#EXTM3U
#EXT-X-STREAM-INF:BANDWIDTH=460800,RESOLUTION=480x270
low.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=1228800,RESOLUTION=1280x720
mid.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=2048000,RESOLUTION=1920x1080
high.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=4000000,RESOLUTION=3840x2160
uhd.m3u8
)";

    const auto qualityUrls = APIServiceTestAdapter::parseM3U8QualityUrls(apiService, m3u8Payload, QString("https://dh5.example/asp/enc2/index.m3u8"));

    QCOMPARE(qualityUrls.size(), 4);
    QCOMPARE(qualityUrls.value(QString("4")), QString("low.m3u8"));
    QCOMPARE(qualityUrls.value(QString("2")), QString("mid.m3u8"));
    QCOMPARE(qualityUrls.value(QString("1")), QString("high.m3u8"));
    QCOMPARE(qualityUrls.value(QString("5")), QString("uhd.m3u8"));

    const auto selected = APIServiceTestAdapter::selectQuality(apiService, QString("0"), qualityUrls);
    QCOMPARE(selected, QString("5"));
}

void CoreRegressionTests::apiservice_getPlayColumnInfo_usesGuidFallbackForCctv4k()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    const QUrl url(QStringLiteral("https://tv.cctv.com/cctv4k/example.shtml"));
    const QByteArray html = R"(
<html><head><script>
var guid = '4k-guid-001';
</script></head></html>
)";

    manager.queueSuccess(url, html);
    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);

    auto result = apiService.getPlayColumnInfo(url.toString());

    QVERIFY(!result.isNull());
    QCOMPARE(result->size(), 3);
    QCOMPARE(result->at(0), QString("CCTV-4K"));
    QCOMPARE(result->at(1), QString("4k-guid-001"));
    QCOMPARE(result->at(2), QString("4k-guid-001"));
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::apiservice_getVideoList_usesCctv4kGuidFallback()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    const QString guid = QStringLiteral("4k-guid-002");
    const QString itemId = QStringLiteral("4k-item-002");
    const QString date = QStringLiteral("202604");

    manager.queueSuccess(APIServiceTestAdapter::buildVideoApiUrl(apiService, FetchType::Column, guid, date, 1, 100),
                         QByteArray(R"({"data":{"list":[],"total":0}})"));

    QUrl albumUrl(QStringLiteral("https://api.cntv.cn/NewVideoset/getVideoAlbumInfoByVideoId"));
    QUrlQuery albumQuery;
    albumQuery.addQueryItem(QStringLiteral("id"), itemId);
    albumQuery.addQueryItem(QStringLiteral("serviceId"), QStringLiteral("tvcctv"));
    albumUrl.setQuery(albumQuery);
    manager.queueSuccess(albumUrl, QByteArray(R"({"data":{}})"));

    QUrl videoInfoUrl(QStringLiteral("https://zy.api.cntv.cn/video/videoinfoByGuid"));
    QUrlQuery videoInfoQuery;
    videoInfoQuery.addQueryItem(QStringLiteral("serviceId"), QStringLiteral("cctv4k"));
    videoInfoQuery.addQueryItem(QStringLiteral("guid"), guid);
    videoInfoUrl.setQuery(videoInfoQuery);
    manager.queueSuccess(videoInfoUrl, QByteArray(R"({"vid":"4k-video-002","title":"CCTV-4K Sample","brief":"brief","img":"image.jpg","time":"2026-04-23"})"));

    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);

    const auto videos = apiService.getVideoList(guid, itemId, date, date);

    QCOMPARE(videos.size(), 1);
    QCOMPARE(videos.value(0).guid, QString("4k-video-002"));
    QCOMPARE(videos.value(0).title, QString("CCTV-4K Sample"));
    QCOMPARE(videos.value(0).brief, QString("brief"));
    QCOMPARE(videos.value(0).image, QString("image.jpg"));
    QCOMPARE(videos.value(0).time, QString("2026-04-23"));
    QCOMPARE(manager.requestCount(), 3);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::apiservice_startGetPlayColumnInfo_asyncSuccess_emitsResolvedData()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    const QUrl url(QStringLiteral("https://tv.cctv.com/cctv4k/async-example.shtml"));
    const QByteArray html = R"(
<html><head><script>
var guid = '4k-guid-async-import';
</script></head></html>
)";

    manager.queueSuccess(url, html);
    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);

    QSignalSpy resolvedSpy(&apiService, &APIService::playColumnInfoResolved);
    QSignalSpy failedSpy(&apiService, &APIService::playColumnInfoFailed);

    const quint64 requestId = apiService.startGetPlayColumnInfo(url.toString());

    QVERIFY(resolvedSpy.wait(1000));
    QCOMPARE(resolvedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 0);

    const QList<QVariant> resultArgs = resolvedSpy.takeFirst();
    QCOMPARE(resultArgs.at(0).toULongLong(), requestId);
    QCOMPARE(resultArgs.at(1).toStringList(), QStringList({
        QStringLiteral("CCTV-4K"),
        QStringLiteral("4k-guid-async-import"),
        QStringLiteral("4k-guid-async-import")
    }));
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::apiservice_startGetBrowseVideoList_asyncSuccess_preservesHighlightAndFragmentBrowseSemantics()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    const QString columnId = QStringLiteral("TOPC-browse-001");
    const QString itemId = QStringLiteral("VIDE-browse-001");
    const QString date = QStringLiteral("202604");

    manager.queueSuccess(APIServiceTestAdapter::buildVideoApiUrl(apiService, FetchType::Column, columnId, date, 1, 100),
        QByteArray(R"({"data":{"list":[{"guid":"main-guid","title":"Main Video","image":"main.jpg","brief":"main brief","time":"2026-04-01"}],"total":1}})"));

    QUrl albumUrl(QStringLiteral("https://api.cntv.cn/NewVideoset/getVideoAlbumInfoByVideoId"));
    QUrlQuery albumQuery;
    albumQuery.addQueryItem(QStringLiteral("id"), itemId);
    albumQuery.addQueryItem(QStringLiteral("serviceId"), QStringLiteral("tvcctv"));
    albumUrl.setQuery(albumQuery);
    manager.queueSuccess(albumUrl, QByteArray(R"({"data":{"id":"album-browse-001"}})"));

    manager.queueSuccess(APIServiceTestAdapter::buildAlbumVideoListUrl(apiService, QStringLiteral("album-browse-001"), 1, 1, 100),
        QByteArray(R"({"data":{"list":[{"guid":"highlight-guid","title":"Highlight Video","image":"highlight.jpg","brief":"highlight brief","time":"2026-04-02"},{"guid":"main-guid","title":"Main Video Duplicate","image":"dup.jpg","brief":"dup brief","time":"2026-04-03"}],"total":2}})"));

    manager.queueSuccess(APIServiceTestAdapter::buildTopicVideoListUrl(apiService, columnId, itemId, 1),
        QByteArray(R"({"data":[{"guid":"fragment-guid","video_title":"Fragment Video","video_key_frame_url":"fragment.jpg","sc":"fragment brief","video_shared_code":"2026-04-04"}]})"));

    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);

    QSignalSpy resolvedSpy(&apiService, &APIService::browseVideoListResolved);

    const quint64 requestId = apiService.startGetBrowseVideoList(columnId, itemId, date, date, true);

    QVERIFY(resolvedSpy.wait(1000));
    QCOMPARE(resolvedSpy.count(), 1);

    const QList<QVariant> resultArgs = resolvedSpy.takeFirst();
    QCOMPARE(resultArgs.at(0).toULongLong(), requestId);
    const QMap<int, VideoItem> videos = resultArgs.at(1).value<QMap<int, VideoItem>>();
    QCOMPARE(videos.size(), 3);
    QCOMPARE(videos.value(0).guid, QStringLiteral("main-guid"));
    QCOMPARE(videos.value(1).guid, QStringLiteral("highlight-guid"));
    QCOMPARE(videos.value(1).isHighlight, true);
    QCOMPARE(videos.value(1).listType, QStringLiteral("看点"));
    QCOMPARE(videos.value(2).guid, QStringLiteral("fragment-guid"));
    QCOMPARE(videos.value(2).isHighlight, true);
    QCOMPARE(videos.value(2).listType, QStringLiteral("片段"));
    QCOMPARE(manager.requestCount(), 4);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::apiservice_startGetImage_asyncSuccess_emitsLoadedImage()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    const QUrl url(QStringLiteral("https://example.test/preview.png"));

    QImage sourceImage(4, 3, QImage::Format_RGB32);
    sourceImage.fill(Qt::red);
    QByteArray encodedImage;
    QBuffer buffer(&encodedImage);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(sourceImage.save(&buffer, "PNG"));

    manager.queueSuccess(url, encodedImage);
    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);

    QSignalSpy resolvedSpy(&apiService, &APIService::imageResolved);

    const quint64 requestId = apiService.startGetImage(url.toString());

    QVERIFY(resolvedSpy.wait(1000));
    QCOMPARE(resolvedSpy.count(), 1);

    const QList<QVariant> resultArgs = resolvedSpy.takeFirst();
    QCOMPARE(resultArgs.at(0).toULongLong(), requestId);
    QCOMPARE(resultArgs.at(1).toString(), url.toString());
    const QImage image = qvariant_cast<QImage>(resultArgs.at(2));
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), sourceImage.size());
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::apiservice_startGetEncryptM3U8Urls_asyncSuccess_emitsUrlsAnd4KFlag()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    const QString guid = QStringLiteral("4k-guid-async-001");

    QUrl infoUrl(QStringLiteral("https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do"));
    QUrlQuery infoQuery;
    infoQuery.addQueryItem(QStringLiteral("pid"), guid);
    infoUrl.setQuery(infoQuery);
    manager.queueSuccess(infoUrl, QByteArray(R"({"play_channel":"CCTV-4K","hls_url":"https://4k.example/live/main/index.m3u8"})"));

    const QUrl playlistUrl(QStringLiteral("https://4k.example/live/4000/index.m3u8"));
    manager.queueSuccess(playlistUrl, QByteArray("#EXTM3U\r\n#EXTINF:2.0,\r\n0.ts?maxbr=2048\r\n#EXTINF:2.0,\r\n1.ts?maxbr=2048\r\n"));

    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);

    QSignalSpy resolvedSpy(&apiService, &APIService::encryptM3U8UrlsResolved);
    QSignalSpy failedSpy(&apiService, &APIService::encryptM3U8UrlsFailed);
    QSignalSpy cancelledSpy(&apiService, &APIService::encryptM3U8UrlsCancelled);

    apiService.startGetEncryptM3U8Urls(guid, QStringLiteral("0"));

    QVERIFY(resolvedSpy.wait(1000));
    QCOMPARE(resolvedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(cancelledSpy.count(), 0);

    const QList<QVariant> resultArgs = resolvedSpy.takeFirst();
    QCOMPARE(resultArgs.at(0).toStringList(), QStringList({
        QStringLiteral("https://4k.example/live/4000/0.ts?maxbr=2048"),
        QStringLiteral("https://4k.example/live/4000/1.ts?maxbr=2048")
    }));
    QVERIFY(resultArgs.at(1).toBool());
    QVERIFY(apiService.lastM3U8ResultWas4K());
    QCOMPARE(manager.requestCount(), 2);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::apiservice_startGetEncryptM3U8Urls_asyncFailure_emitsExactlyOnce()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    const QString guid = QStringLiteral("async-failure-guid-001");

    QUrl infoUrl(QStringLiteral("https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do"));
    QUrlQuery infoQuery;
    infoQuery.addQueryItem(QStringLiteral("pid"), guid);
    infoUrl.setQuery(infoQuery);
    manager.queueSuccess(infoUrl, QByteArray(R"({"manifest":{}})"));

    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);

    QSignalSpy resolvedSpy(&apiService, &APIService::encryptM3U8UrlsResolved);
    QSignalSpy failedSpy(&apiService, &APIService::encryptM3U8UrlsFailed);
    QSignalSpy cancelledSpy(&apiService, &APIService::encryptM3U8UrlsCancelled);

    apiService.startGetEncryptM3U8Urls(guid, QStringLiteral("0"));

    QVERIFY(failedSpy.wait(1000));
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(resolvedSpy.count(), 0);
    QCOMPARE(cancelledSpy.count(), 0);
    QCOMPARE(failedSpy.takeFirst().at(0).toString(), QStringLiteral("无法获取hls_enc2_url"));
    QVERIFY(!apiService.lastM3U8ResultWas4K());
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::apiservice_cancelGetEncryptM3U8Urls_abortsPendingReplyAndSuppressesSuccess()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    const QString guid = QStringLiteral("async-cancel-guid-001");

    QUrl infoUrl(QStringLiteral("https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do"));
    QUrlQuery infoQuery;
    infoQuery.addQueryItem(QStringLiteral("pid"), guid);
    infoUrl.setQuery(infoQuery);
    manager.queueSuccess(infoUrl, QByteArray(R"({"play_channel":"CCTV-4K","hls_url":"https://4k.example/live/main/index.m3u8"})"), 200);

    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);

    QSignalSpy resolvedSpy(&apiService, &APIService::encryptM3U8UrlsResolved);
    QSignalSpy failedSpy(&apiService, &APIService::encryptM3U8UrlsFailed);
    QSignalSpy cancelledSpy(&apiService, &APIService::encryptM3U8UrlsCancelled);

    apiService.startGetEncryptM3U8Urls(guid, QStringLiteral("0"));

    QTRY_VERIFY_WITH_TIMEOUT(manager.lastReply() != nullptr, 1000);
    FakeNetworkReply* pendingReply = manager.lastReply();
    QVERIFY(pendingReply != nullptr);

    apiService.cancelGetEncryptM3U8Urls();

    QCOMPARE(cancelledSpy.count(), 1);
    QVERIFY(pendingReply->wasAborted());
    QTest::qWait(250);
    QCOMPARE(cancelledSpy.count(), 1);
    QCOMPARE(resolvedSpy.count(), 0);
    QCOMPARE(failedSpy.count(), 0);
    QVERIFY(!apiService.lastM3U8ResultWas4K());
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::apiservice_getEncryptM3U8Urls_cctv4kUses4000Playlist()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    const QString guid = QStringLiteral("4k-guid-003");

    QUrl infoUrl(QStringLiteral("https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do"));
    QUrlQuery infoQuery;
    infoQuery.addQueryItem(QStringLiteral("pid"), guid);
    infoUrl.setQuery(infoQuery);
    manager.queueSuccess(infoUrl, QByteArray(R"({"play_channel":"CCTV-4K","hls_url":"https://4k.example/live/main/index.m3u8"})"));

    const QUrl playlistUrl(QStringLiteral("https://4k.example/live/4000/index.m3u8"));
    manager.queueSuccess(playlistUrl, QByteArray("#EXTM3U\r\n#EXTINF:2.0,\r\n0.ts?maxbr=2048\r\n#EXTINF:2.0,\r\n1.ts?maxbr=2048\r\n"));

    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);

    const auto tsUrls = APIServiceTestAdapter::getEncryptM3U8Urls(apiService, guid, QStringLiteral("0"));

    QCOMPARE(tsUrls.size(), 2);
    QCOMPARE(tsUrls.at(0), QString("https://4k.example/live/4000/0.ts?maxbr=2048"));
    QCOMPARE(tsUrls.at(1), QString("https://4k.example/live/4000/1.ts?maxbr=2048"));
    QVERIFY(apiService.lastM3U8ResultWas4K());
    QCOMPARE(manager.requestCount(), 2);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::apiservice_buildVideoApiUrl_buildsExpectedQuery()
{
    APIService& apiService = APIService::instance();

    const auto columnUrl = APIServiceTestAdapter::buildVideoApiUrl(apiService, FetchType::Column, QString("column-id"), QString("202501"), 2, 50);
    QCOMPARE(columnUrl.host(), QString("api.cntv.cn"));
    QCOMPARE(columnUrl.path(), QString("/NewVideo/getVideoListByColumn"));
    const auto columnQuery = QUrlQuery(columnUrl);
    QCOMPARE(columnQuery.queryItemValue(QString("id")), QString("column-id"));
    QCOMPARE(columnQuery.queryItemValue(QString("d")), QString("202501"));
    QCOMPARE(columnQuery.queryItemValue(QString("p")), QString("2"));
    QCOMPARE(columnQuery.queryItemValue(QString("n")), QString("50"));
    QCOMPARE(columnQuery.queryItemValue(QString("sort")), QString("desc"));

    const auto albumUrl = APIServiceTestAdapter::buildVideoApiUrl(apiService, FetchType::Album, QString("album-id"), QString("202412"), 1, 100);
    QCOMPARE(albumUrl.path(), QString("/NewVideo/getVideoListByAlbumIdNew"));
    const auto albumQuery = QUrlQuery(albumUrl);
    QCOMPARE(albumQuery.queryItemValue(QString("id")), QString("album-id"));
    QCOMPARE(albumQuery.queryItemValue(QString("pub")), QString("1"));
    QCOMPARE(albumQuery.queryItemValue(QString("sort")), QString("asc"));

    const auto pagedUrl = APIServiceTestAdapter::buildVideoApiUrl(apiService, FetchType::Column, QString("TOPC1451559129520755"), QString("202602"), 4, 100);
    const auto pagedQuery = QUrlQuery(pagedUrl);
    QCOMPARE(pagedQuery.queryItemValue(QString("p")), QString("4"));
    QCOMPARE(pagedQuery.queryItemValue(QString("n")), QString("100"));
}

void CoreRegressionTests::apiservice_buildAlbumVideoListUrl_buildsHighlightQuery()
{
    APIService& apiService = APIService::instance();

    const auto url = APIServiceTestAdapter::buildAlbumVideoListUrl(apiService, QString("album-id"), 1, 3, 25);
    QCOMPARE(url.host(), QString("api.cntv.cn"));
    QCOMPARE(url.path(), QString("/NewVideo/getVideoListByAlbumIdNew"));

    const auto query = QUrlQuery(url);
    QCOMPARE(query.queryItemValue(QString("id")), QString("album-id"));
    QCOMPARE(query.queryItemValue(QString("serviceId")), QString("tvcctv"));
    QCOMPARE(query.queryItemValue(QString("pub")), QString("1"));
    QCOMPARE(query.queryItemValue(QString("sort")), QString("asc"));
    QCOMPARE(query.queryItemValue(QString("mode")), QString("1"));
    QCOMPARE(query.queryItemValue(QString("p")), QString("3"));
    QCOMPARE(query.queryItemValue(QString("n")), QString("25"));
}

void CoreRegressionTests::apiservice_buildTopicVideoListUrl_buildsFragmentQuery()
{
    APIService& apiService = APIService::instance();

    const auto url = APIServiceTestAdapter::buildTopicVideoListUrl(apiService, QString("TOPC1451550970356385"), QString("VIDER7mPB8nmQTvlV8rXlov0220624"), 1);
    QCOMPARE(url.host(), QString("api.cntv.cn"));
    QCOMPARE(url.path(), QString("/video/getVideoListByTopicIdInfo"));

    const auto query = QUrlQuery(url);
    QCOMPARE(query.queryItemValue(QString("videoid")), QString("VIDER7mPB8nmQTvlV8rXlov0220624"));
    QCOMPARE(query.queryItemValue(QString("topicid")), QString("TOPC1451550970356385"));
    QCOMPARE(query.queryItemValue(QString("serviceId")), QString("tvcctv"));
    QCOMPARE(query.queryItemValue(QString("type")), QString("1"));
}

void CoreRegressionTests::apiservice_buildTsUrlsFromPlaylistData_returnsExpectedAbsoluteUrls()
{
    APIService& apiService = APIService::instance();
    const QByteArray playlistData("#EXTM3U\r\n#EXTINF:2.0,\r\nsegment-0001.ts?maxbr=2048\r\n#EXTINF:2.0,\r\nsegment-0002.ts?maxbr=2048\r\n");

    const auto tsUrls = APIServiceTestAdapter::buildTsUrlsFromPlaylistData(apiService, playlistData, QString("https://example.com/path/video/index.m3u8?maxbr=2048"));

    QCOMPARE(tsUrls.size(), 2);
    QCOMPARE(tsUrls.at(0), QString("https://example.com/path/video/segment-0001.ts?maxbr=2048"));
    QCOMPARE(tsUrls.at(1), QString("https://example.com/path/video/segment-0002.ts?maxbr=2048"));
}

void CoreRegressionTests::apiservice_sendNetworkRequest_fakeSuccess_returnsDeterministicBody()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    const QUrl url(QStringLiteral("https://fake.test/apiservice-success.json"));
    const QByteArray expectedBody(R"({"result":"ok","source":"fake-manager"})");

    manager.queueSuccess(url, expectedBody);
    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);

    const QByteArray response = APIServiceTestAdapter::sendNetworkRequest(apiService, url);

    QCOMPARE(response, expectedBody);
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
    QCOMPARE(manager.requestedUrls().constFirst(), url);
}

void CoreRegressionTests::apiservice_sendNetworkRequest_fakeError_returnsEmptyData()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    const QUrl url(QStringLiteral("https://fake.test/apiservice-error.json"));

    manager.queueError(url, QNetworkReply::ConnectionRefusedError, QStringLiteral("deterministic connection refused"));
    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);

    const QByteArray response = APIServiceTestAdapter::sendNetworkRequest(apiService, url);

    QVERIFY2(response.isEmpty(), "network errors should preserve the current empty-byte-array failure contract");
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
    QCOMPARE(manager.requestedUrls().constFirst(), url);
}

void CoreRegressionTests::apiservice_getTsFileList_returnsExpectedUrlsFromSyntheticData()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;

    const QByteArray syntheticPlaylist = R"(
#EXTM3U
#EXTINF:2.0,
segment-0001.ts
#EXTINF:2.0,
segment-0002.ts
)";

    const QString qualityPath = QString("/asp/enc/video-123/index.m3u8");
    const QString baseUrl = QString("https://dh5.example.com/asp/enc/video-123/index.m3u8");
    const QUrl expectedM3u8Url(QStringLiteral("https://dh5.example.com/asp/enc/video-123/index.m3u8"));

    manager.queueSuccess(expectedM3u8Url, syntheticPlaylist);
    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);

    const auto tsUrls = APIServiceTestAdapter::getTsFileList(apiService, qualityPath, baseUrl);

    QCOMPARE(tsUrls.size(), 2);
    QCOMPARE(tsUrls.at(0), QString("https://dh5.example.com/asp/enc/video-123/segment-0001.ts"));
    QCOMPARE(tsUrls.at(1), QString("https://dh5.example.com/asp/enc/video-123/segment-0002.ts"));
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
    QCOMPARE(manager.requestedUrls().constFirst(), expectedM3u8Url);
}

void CoreRegressionTests::fakeNetworkReply_success_emitsReadyReadProgressAndFinished()
{
    FakeNetworkAccessManager manager;
    const QUrl url(QStringLiteral("https://fake.test/success.bin"));
    const QByteArray body("deterministic-body");
    manager.queueSuccess(url, body);

    QNetworkReply* reply = manager.get(QNetworkRequest(url));
    QVERIFY(reply != nullptr);

    QSignalSpy readyReadSpy(reply, &QNetworkReply::readyRead);
    QSignalSpy progressSpy(reply, &QNetworkReply::downloadProgress);
    QSignalSpy finishedSpy(reply, &QNetworkReply::finished);

    QVERIFY(finishedSpy.wait(1000));
    QCOMPARE(reply->error(), QNetworkReply::NoError);
    QCOMPARE(reply->readAll(), body);
    QCOMPARE(readyReadSpy.count(), 1);
    QVERIFY(progressSpy.count() >= 1);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);

    const QList<QVariant> lastProgress = progressSpy.takeLast();
    QCOMPARE(lastProgress.at(0).toLongLong(), body.size());
    QCOMPARE(lastProgress.at(1).toLongLong(), body.size());
}

void CoreRegressionTests::fakeNetworkReply_abort_marksCancelledAndFinishes()
{
    FakeNetworkAccessManager manager;
    const QUrl url(QStringLiteral("https://fake.test/pending.bin"));
    manager.queueSuccess(url, QByteArray("pending-data"), 200);

    QNetworkReply* reply = manager.get(QNetworkRequest(url));
    QVERIFY(reply != nullptr);

    auto* fakeReply = qobject_cast<FakeNetworkReply*>(reply);
    QVERIFY(fakeReply != nullptr);

    QSignalSpy finishedSpy(reply, &QNetworkReply::finished);
    QSignalSpy errorSpy(reply, &QNetworkReply::errorOccurred);

    fakeReply->abort();

    QVERIFY(finishedSpy.count() == 1 || finishedSpy.wait(1000));
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(fakeReply->wasAborted());
    QCOMPARE(reply->error(), QNetworkReply::OperationCanceledError);
    QCOMPARE(reply->readAll(), QByteArray());
}

void CoreRegressionTests::fakeNetworkAccessManager_queueErrorAndUnexpectedRequestFailDeterministically()
{
    {
        FakeNetworkAccessManager manager;
        const QUrl queuedUrl(QStringLiteral("https://fake.test/error.json"));
        manager.queueError(queuedUrl, QNetworkReply::ConnectionRefusedError, QStringLiteral("queued connection refused"));

        QNetworkReply* reply = manager.get(QNetworkRequest(queuedUrl));
        QVERIFY(reply != nullptr);

        QSignalSpy finishedSpy(reply, &QNetworkReply::finished);
        QVERIFY(finishedSpy.wait(1000));

        QCOMPARE(reply->error(), QNetworkReply::ConnectionRefusedError);
        QCOMPARE(reply->errorString(), QStringLiteral("queued connection refused"));
        QCOMPARE(manager.unexpectedRequestCount(), 0);
        QCOMPARE(manager.queuedReplyCount(), 0);
    }

    {
        FakeNetworkAccessManager manager;
        manager.queueSuccess(QUrl(QStringLiteral("https://fake.test/expected.json")), QByteArray("never-consumed"));

        QNetworkReply* reply = manager.get(QNetworkRequest(QUrl(QStringLiteral("https://fake.test/unexpected.json"))));
        QVERIFY(reply != nullptr);

        QSignalSpy finishedSpy(reply, &QNetworkReply::finished);
        QVERIFY(finishedSpy.wait(1000));

        QCOMPARE(reply->error(), QNetworkReply::ProtocolFailure);
        QVERIFY(reply->errorString().contains(QStringLiteral("Unexpected request")));
        QCOMPARE(manager.unexpectedRequestCount(), 1);
        QCOMPARE(manager.queuedReplyCount(), 1);
        QCOMPARE(manager.requestedUrls().constFirst(), QUrl(QStringLiteral("https://fake.test/unexpected.json")));
    }
}

void CoreRegressionTests::fakeNetworkAccessManager_delayedFinish_waitsUntilQueuedCompletion()
{
    FakeNetworkAccessManager manager;
    const QUrl url(QStringLiteral("https://fake.test/delayed.bin"));
    manager.queueSuccess(url, QByteArray("slow-body"), 40);

    QElapsedTimer elapsed;
    elapsed.start();

    QNetworkReply* reply = manager.get(QNetworkRequest(url));
    QVERIFY(reply != nullptr);

    QSignalSpy finishedSpy(reply, &QNetworkReply::finished);
    QCOMPARE(finishedSpy.count(), 0);
    QTest::qWait(10);
    QCOMPARE(finishedSpy.count(), 0);
    QVERIFY(finishedSpy.wait(1000));
    QVERIFY(elapsed.elapsed() >= 30);
    QCOMPARE(reply->error(), QNetworkReply::NoError);
}

void CoreRegressionTests::concatWorker_success_stagesResultTs()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString firstTsPath = QDir(tempDir.path()).filePath(QStringLiteral("2.ts"));
    const QString secondTsPath = QDir(tempDir.path()).filePath(QStringLiteral("10.ts"));
    QVERIFY(createFakeTsFile(firstTsPath, 2, 0));
    QVERIFY(createFakeTsFile(secondTsPath, 2, 256));

    ConcatWorker worker;
    QSignalSpy spy(&worker, &ConcatWorker::concatFinished);

    worker.setFilePath(tempDir.path());
    worker.doConcat();

    QCOMPARE(spy.count(), 1);
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
    QVERIFY(arguments.at(1).toString().contains(QStringLiteral("result.ts")));

    const QString resultTsPath = QDir(tempDir.path()).filePath(QStringLiteral("result.ts"));
    QVERIFY(QFileInfo::exists(resultTsPath));
    QVERIFY(!QFileInfo::exists(QDir(tempDir.path()).filePath(QStringLiteral("result.mp4"))));

    const MediaContainerValidationResult validation = MediaContainerValidator::validateFile(resultTsPath, MediaContainerType::MpegTs);
    QVERIFY2(validation.ok, qPrintable(validation.code + QStringLiteral(": ") + validation.message));
}

void CoreRegressionTests::concatWorker_zeroByteFile_emitsFailure()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString zeroFile = QDir(tempDir.path()).filePath("0000.ts");
    QVERIFY(createEmptyFile(zeroFile));

    ConcatWorker worker;
    QSignalSpy spy(&worker, &ConcatWorker::concatFinished);

    worker.setFilePath(tempDir.path());
    worker.doConcat();

    QCOMPARE(spy.count(), 1);
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QVERIFY(arguments.at(1).toString().contains(QStringLiteral("空文件")));
    QVERIFY(!QFileInfo::exists(QDir(tempDir.path()).filePath(QStringLiteral("result.ts"))));
    QVERIFY(!QFileInfo::exists(QDir(tempDir.path()).filePath(QStringLiteral("result.mp4"))));
}

void CoreRegressionTests::concatWorker_cancelDuringMerge_emitsCancelledAndDoesNotStageResultTs()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString firstTsPath = QDir(tempDir.path()).filePath(QStringLiteral("1.ts"));
    const QString secondTsPath = QDir(tempDir.path()).filePath(QStringLiteral("2.ts"));
    QVERIFY(createFakeTsFile(firstTsPath, 256, 0));
    QVERIFY(createFakeTsFile(secondTsPath, 256, 256));

    ConcatWorker worker;
    QSignalSpy spy(&worker, &ConcatWorker::concatFinished);

    bool cancelIssued = false;
    setTsMergerTestPacketProcessedHook([&]() {
        if (!cancelIssued) {
            cancelIssued = true;
            worker.cancelConcat();
        }
    });

    worker.setFilePath(tempDir.path());
    worker.doConcat();
    clearTsMergerTestPacketProcessedHook();

    QCOMPARE(spy.count(), 1);
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QStringLiteral("cancelled"));
    QVERIFY(!QFileInfo::exists(QDir(tempDir.path()).filePath(QStringLiteral("result.ts"))));
    QVERIFY(!QFileInfo::exists(QDir(tempDir.path()).filePath(QStringLiteral("result.mp4"))));
}

void CoreRegressionTests::tsMerger_zeroByteFile_returnsFalse()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString zeroFile = QDir(tempDir.path()).filePath("0000.ts");
    QVERIFY(createEmptyFile(zeroFile));

    const QString outputPath = QDir(tempDir.path()).filePath("result.ts");

    TSMerger merger;
    merger.reset();

    const std::vector<QString> files = {zeroFile};
    QVERIFY(!merger.merge(files, outputPath));
}

void CoreRegressionTests::tsMerger_malformedNonZeroFile_returnsFalse()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString malformedFile = QDir(tempDir.path()).filePath("bad-sync.ts");
    {
        QFile file(malformedFile);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QByteArray packet(188, '\0');
        packet[0] = static_cast<char>(0x00);
        QCOMPARE(file.write(packet), 188);
    }

    const QString outputPath = QDir(tempDir.path()).filePath("result.ts");

    TSMerger merger;
    merger.reset();

    const std::vector<QString> files = {malformedFile};
    QVERIFY(!merger.merge(files, outputPath));
    QVERIFY(!QFileInfo::exists(outputPath));
}

void CoreRegressionTests::tsMerger_failedMerge_preservesExistingOutputFile()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString malformedFile = QDir(tempDir.path()).filePath("bad-sync.ts");
    {
        QFile file(malformedFile);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QByteArray packet(188, '\0');
        packet[0] = static_cast<char>(0x00);
        QCOMPARE(file.write(packet), 188);
    }

    const QString outputPath = QDir(tempDir.path()).filePath("result.ts");
    const QByteArray existingBody("previous merged output");
    {
        QFile file(outputPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(existingBody), qint64(existingBody.size()));
    }

    TSMerger merger;
    merger.reset();

    const std::vector<QString> files = {malformedFile};
    QVERIFY(!merger.merge(files, outputPath));

    QFile outputFile(outputPath);
    QVERIFY(outputFile.open(QIODevice::ReadOnly));
    QCOMPARE(outputFile.readAll(), existingBody);
}

void CoreRegressionTests::tsMerger_validMinimalPacket_succeeds()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString validFile = QDir(tempDir.path()).filePath("0000.ts");
    QVERIFY(createFakeTsFile(validFile, 4, 0));

    const QString outputPath = QDir(tempDir.path()).filePath("result.ts");

    TSMerger merger;
    merger.reset();

    const std::vector<QString> files = {validFile};
    QVERIFY(merger.merge(files, outputPath));
    QVERIFY(QFileInfo::exists(outputPath));
    QVERIFY(QFileInfo(outputPath).size() > 0);

    const MediaContainerValidationResult validation = MediaContainerValidator::validateFile(outputPath, MediaContainerType::MpegTs);
    QVERIFY2(validation.ok, qPrintable(validation.code + QStringLiteral(": ") + validation.message));
}

void CoreRegressionTests::mediaContainerValidator_validTs_detectsMpegTs()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString tsFilePath = QDir(tempDir.path()).filePath("sample.ts");
    QVERIFY(createFakeTsFile(tsFilePath, 4, 256));

    const MediaContainerValidationResult result = MediaContainerValidator::validateFile(tsFilePath, MediaContainerType::MpegTs);

    QVERIFY2(result.ok, qPrintable(result.code + QStringLiteral(": ") + result.message));
    QCOMPARE(result.expectedType, MediaContainerType::MpegTs);
    QCOMPARE(result.detectedType, MediaContainerType::MpegTs);
    QCOMPARE(result.code, QStringLiteral("container_match"));
}

void CoreRegressionTests::mediaContainerValidator_validMp4_detectsMp4()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString mp4FilePath = QDir(tempDir.path()).filePath("sample.mp4");
    QVERIFY(createFileWithContents(mp4FilePath, createFakeMp4Bytes()));

    const MediaContainerValidationResult result = MediaContainerValidator::validateFile(mp4FilePath, MediaContainerType::Mp4);

    QVERIFY2(result.ok, qPrintable(result.code + QStringLiteral(": ") + result.message));
    QCOMPARE(result.expectedType, MediaContainerType::Mp4);
    QCOMPARE(result.detectedType, MediaContainerType::Mp4);
    QCOMPARE(result.code, QStringLiteral("container_match"));
}

void CoreRegressionTests::mediaContainerValidator_tsBytesRenamedMp4_rejectsAsMp4()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString fakeMp4Path = QDir(tempDir.path()).filePath("renamed.mp4");
    QVERIFY(createFakeTsFile(fakeMp4Path, 4, 256));

    const MediaContainerValidationResult result = MediaContainerValidator::validateFile(fakeMp4Path, MediaContainerType::Mp4);

    QVERIFY(!result.ok);
    QCOMPARE(result.expectedType, MediaContainerType::Mp4);
    QCOMPARE(result.detectedType, MediaContainerType::MpegTs);
    QCOMPARE(result.code, QStringLiteral("unexpected_container"));
}

void CoreRegressionTests::mediaContainerValidator_mixedFtypAndTsSync_rejectsAsMp4()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QByteArray mixedSignatureBytes = createFakeMp4Bytes();
    for (int i = 0; i < 4; ++i) {
        QByteArray packet(188, '\0');
        packet[0] = 0x47;
        packet[3] = 0x10;
        mixedSignatureBytes.append(packet);
    }

    const QString mixedPath = QDir(tempDir.path()).filePath("mixed-signature.mp4");
    QVERIFY(createFileWithContents(mixedPath, mixedSignatureBytes));

    const MediaContainerValidationResult result = MediaContainerValidator::validateFile(mixedPath, MediaContainerType::Mp4);

    QVERIFY(!result.ok);
    QCOMPARE(result.expectedType, MediaContainerType::Mp4);
    QCOMPARE(result.detectedType, MediaContainerType::MpegTs);
    QCOMPARE(result.code, QStringLiteral("unexpected_container"));
}

void CoreRegressionTests::mediaContainerValidator_invalidFiles_rejectEmptyAndUnknown()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString emptyFilePath = QDir(tempDir.path()).filePath("empty.bin");
    QVERIFY(createEmptyFile(emptyFilePath));

    const MediaContainerValidationResult emptyResult = MediaContainerValidator::validateFile(emptyFilePath, MediaContainerType::Mp4);

    QVERIFY(!emptyResult.ok);
    QCOMPARE(emptyResult.expectedType, MediaContainerType::Mp4);
    QCOMPARE(emptyResult.detectedType, MediaContainerType::Unknown);
    QCOMPARE(emptyResult.code, QStringLiteral("empty_file"));

    const QString invalidFilePath = QDir(tempDir.path()).filePath("invalid.bin");
    QVERIFY(createFileWithContents(invalidFilePath, QByteArray("not a media container")));

    const MediaContainerValidationResult invalidResult = MediaContainerValidator::validateFile(invalidFilePath, MediaContainerType::Mp4);

    QVERIFY(!invalidResult.ok);
    QCOMPARE(invalidResult.expectedType, MediaContainerType::Mp4);
    QCOMPARE(invalidResult.detectedType, MediaContainerType::Unknown);
    QCOMPARE(invalidResult.code, QStringLiteral("unknown_container"));
}

void CoreRegressionTests::directMediaFinalizer_whitespaceTitle_usesProducerHashContract()
{
    initializeSettingsSandbox();

    const QString rawTitle = QStringLiteral("  CCTV-4K 空白标题  ");
    const QString trimmedTitle = rawTitle.trimmed();
    const QString producerTaskHash = decryptTaskHash(rawTitle);
    const QString trimmedTaskHash = decryptTaskHash(trimmedTitle);
    QVERIFY(producerTaskHash != trimmedTaskHash);

    g_settings->beginGroup("settings");
    g_settings->setValue("save_dir", m_tempDir->path());
    g_settings->setValue("transcode", false);
    g_settings->endGroup();
    g_settings->sync();

    const QString taskDirPath = QDir(m_tempDir->path()).filePath(producerTaskHash);
    QVERIFY(QDir().mkpath(taskDirPath));

    const QString stagingPath = QDir(taskDirPath).filePath(QStringLiteral("result.ts"));
    QVERIFY(createFakeTsFile(stagingPath, 4, 601));

    const DirectMediaFinalizeResult result = finalizeDirectTsTask(rawTitle, m_tempDir->path(), false);

    QVERIFY2(result.ok, qPrintable(result.code + QStringLiteral(": ") + result.message));
    QCOMPARE(result.code, QStringLiteral("published_ts"));

    const QString finalPath = QDir(m_tempDir->path()).filePath(QStringLiteral("%1.ts").arg(trimmedTitle));
    QVERIFY(QFileInfo::exists(finalPath));
    QVERIFY(!QFileInfo::exists(stagingPath));
    QVERIFY(!QFileInfo::exists(taskDirPath));
    QVERIFY(!QFileInfo::exists(QDir(m_tempDir->path()).filePath(trimmedTaskHash)));

    const MediaContainerValidationResult validation = MediaContainerValidator::validateFile(finalPath, MediaContainerType::MpegTs);
    QVERIFY(validation.ok);
}

void CoreRegressionTests::cctvVideoDownloader_cctv4kTsSelection_finalizesStagedTs()
{
    initializeSettingsSandbox();

    g_settings->beginGroup("settings");
    g_settings->setValue("save_dir", m_tempDir->path());
    g_settings->setValue("transcode", false);
    g_settings->endGroup();
    g_settings->sync();

    const QString title = QStringLiteral("CCTV-4K TS");
    const QString taskHash = decryptTaskHash(title);
    const QString taskDirPath = QDir(m_tempDir->path()).filePath(taskHash);
    QVERIFY(QDir().mkpath(taskDirPath));

    const QString stagingPath = QDir(taskDirPath).filePath(QStringLiteral("result.ts"));
    QVERIFY(createFakeTsFile(stagingPath, 4, 600));
    QVERIFY(createFileWithContents(QDir(m_tempDir->path()).filePath(QStringLiteral("output.txt")), QByteArrayLiteral("stale")));

    const DirectMediaFinalizeResult result = finalizeDirectTsTask(title, m_tempDir->path(), false);

    QVERIFY2(result.ok, qPrintable(result.code + QStringLiteral(": ") + result.message));
    QCOMPARE(result.code, QStringLiteral("published_ts"));

    const QString finalPath = QDir(m_tempDir->path()).filePath(QStringLiteral("CCTV-4K TS.ts"));
    QVERIFY(QFileInfo::exists(finalPath));
    QVERIFY(!QFileInfo::exists(stagingPath));
    QVERIFY(!QFileInfo::exists(taskDirPath));
    QVERIFY(!QFileInfo::exists(QDir(m_tempDir->path()).filePath(QStringLiteral("output.txt"))));

    const MediaContainerValidationResult validation = MediaContainerValidator::validateFile(finalPath, MediaContainerType::MpegTs);
    QVERIFY(validation.ok);
}

void CoreRegressionTests::cctvVideoDownloader_cctv4kMp4Selection_remuxesStagedTs()
{
    const QString ffmpegPath = bundledFfmpegPath();
    QVERIFY2(QFileInfo::exists(ffmpegPath), qPrintable(QStringLiteral("Bundled ffmpeg runtime missing at %1").arg(ffmpegPath)));

    initializeSettingsSandbox();

    g_settings->beginGroup("settings");
    g_settings->setValue("save_dir", m_tempDir->path());
    g_settings->setValue("transcode", true);
    g_settings->endGroup();
    g_settings->sync();

    const QString title = QStringLiteral("CCTV-4K MP4");
    const QString taskHash = decryptTaskHash(title);
    const QString taskDirPath = QDir(m_tempDir->path()).filePath(taskHash);
    QVERIFY(QDir().mkpath(taskDirPath));

    const QString stagingPath = QDir(taskDirPath).filePath(QStringLiteral("result.ts"));
    QVERIFY(createFileWithContents(stagingPath, createRemuxableTsFixtureBytes()));
    QVERIFY(MediaContainerValidator::validateFile(stagingPath, MediaContainerType::MpegTs).ok);
    QVERIFY(createFileWithContents(QDir(m_tempDir->path()).filePath(QStringLiteral("output.txt")), QByteArrayLiteral("stale")));

    const DirectMediaFinalizeResult result = finalizeDirectTsTask(title, m_tempDir->path(), true);

    QVERIFY2(result.ok, qPrintable(result.code + QStringLiteral(": ") + result.message));
    QCOMPARE(result.code, QStringLiteral("published_mp4"));

    const QString finalPath = QDir(m_tempDir->path()).filePath(QStringLiteral("CCTV-4K MP4.mp4"));
    QVERIFY(QFileInfo::exists(finalPath));
    QVERIFY(!QFileInfo::exists(finalPath + QStringLiteral(".tmp")));
    QVERIFY(!QFileInfo::exists(stagingPath));
    QVERIFY(!QFileInfo::exists(taskDirPath));
    QVERIFY(!QFileInfo::exists(QDir(m_tempDir->path()).filePath(QStringLiteral("output.txt"))));

    const MediaContainerValidationResult validation = MediaContainerValidator::validateFile(finalPath, MediaContainerType::Mp4);
    QVERIFY(validation.ok);
}

void CoreRegressionTests::mediaFinalizer_publishTs_validatesAndUsesUniqueName()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString stagingPath = QDir(tempDir.path()).filePath("result.ts");
    QVERIFY(createFakeTsFile(stagingPath, 4, 256));

    const QString existingPath = QDir(tempDir.path()).filePath("新闻.ts");
    QVERIFY(createFakeTsFile(existingPath, 4, 257));

    MediaFinalizer finalizer;
    const MediaFinalizeResult result = finalizer.finalize(stagingPath,
        QStringLiteral("新闻"),
        tempDir.path(),
        MediaContainerType::MpegTs);

    QVERIFY(result.ok);
    QCOMPARE(result.code, QStringLiteral("published_ts"));
    QCOMPARE(result.publishedType, MediaContainerType::MpegTs);
    QCOMPARE(QFileInfo(result.finalPath).fileName(), QStringLiteral("新闻(1).ts"));
    QVERIFY(QFileInfo::exists(result.finalPath));
    QVERIFY(!QFileInfo::exists(stagingPath));

    const MediaContainerValidationResult validation = MediaContainerValidator::validateFile(result.finalPath, MediaContainerType::MpegTs);
    QVERIFY(validation.ok);
}

void CoreRegressionTests::mediaFinalizer_remuxesToMp4ThroughBundledCli()
{
    const QString ffmpegPath = bundledFfmpegPath();
    QVERIFY2(QFileInfo::exists(ffmpegPath), qPrintable(QStringLiteral("Bundled ffmpeg runtime missing at %1").arg(ffmpegPath)));

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString stagingPath = QDir(tempDir.path()).filePath("result.ts");
    QVERIFY(createFileWithContents(stagingPath, createRemuxableTsFixtureBytes()));
    QVERIFY(MediaContainerValidator::validateFile(stagingPath, MediaContainerType::MpegTs).ok);

    const QString existingPath = QDir(tempDir.path()).filePath("片段.mp4");
    QVERIFY(createFileWithContents(existingPath, createFakeMp4Bytes()));

    MediaFinalizer finalizer;
    MediaFinalizerTestAdapter::setProcessTimeoutMs(finalizer, 30000);

    const MediaFinalizeResult result = finalizer.finalize(stagingPath,
        QStringLiteral("片段"),
        tempDir.path(),
        MediaContainerType::Mp4);

    QVERIFY2(result.ok, qPrintable(result.code + QStringLiteral(": ") + result.message));
    QCOMPARE(result.code, QStringLiteral("published_mp4"));
    QCOMPARE(result.publishedType, MediaContainerType::Mp4);
    QCOMPARE(QFileInfo(result.finalPath).fileName(), QStringLiteral("片段(1).mp4"));
    QVERIFY(QFileInfo::exists(result.finalPath));
    QVERIFY(!QFileInfo::exists(result.finalPath + QStringLiteral(".tmp")));

    const MediaContainerValidationResult validation = MediaContainerValidator::validateFile(result.finalPath, MediaContainerType::Mp4);
    QVERIFY(validation.ok);
}

void CoreRegressionTests::mediaFinalizer_missingBundledFfmpeg_reportsFailure()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString stagingPath = QDir(tempDir.path()).filePath("result.ts");
    QVERIFY(createFakeTsFile(stagingPath, 4, 256));

    QTemporaryDir emptyAssetsDir;
    QVERIFY(emptyAssetsDir.isValid());

    MediaFinalizer finalizer;
    MediaFinalizerTestAdapter::setTestDecryptAssetsDir(finalizer, emptyAssetsDir.path());

    const MediaFinalizeResult result = finalizer.finalize(stagingPath,
        QStringLiteral("缺失FFmpeg"),
        tempDir.path(),
        MediaContainerType::Mp4);

    QVERIFY(!result.ok);
    QCOMPARE(result.code, QStringLiteral("ffmpeg_missing"));
    QVERIFY(result.message.contains(QStringLiteral("ffmpeg")));
    QVERIFY(!QFileInfo::exists(QDir(tempDir.path()).filePath("缺失FFmpeg.mp4")));

    MediaFinalizerTestAdapter::clearTestDecryptAssetsDir(finalizer);
}

void CoreRegressionTests::mediaFinalizer_remuxTimeout_reportsFailureAndDoesNotPublishMp4()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString stagingPath = QDir(tempDir.path()).filePath("result.ts");
    QVERIFY(createFakeTsFile(stagingPath, 4, 256));

    const QString finalPath = QDir(tempDir.path()).filePath(QStringLiteral("超时Remux.mp4"));
    const QString tempMp4Path = finalPath + QStringLiteral(".tmp");

    MediaFinalizer finalizer;
    bool runnerCalled = false;
    QString observedTempPath;
    MediaFinalizerTestAdapter::setTestProcessRunner(finalizer, [&](const FfmpegCliProcessRequest& request) -> FfmpegCliProcessResult {
        runnerCalled = true;
        observedTempPath = request.arguments.last();
        const bool created = createFileWithContents(tempMp4Path, QByteArrayLiteral("partial mp4 bytes"));

        FfmpegCliProcessResult result;
        result.started = true;
        result.timedOut = true;
        result.stderrText = QStringLiteral("synthetic ffmpeg timeout output");
        if (!created) {
            result.started = false;
            result.timedOut = false;
            result.errorString = QStringLiteral("failed to create synthetic remux temp file");
        }
        return result;
    });

    const MediaFinalizeResult result = finalizer.finalize(stagingPath,
        QStringLiteral("超时Remux"),
        tempDir.path(),
        MediaContainerType::Mp4);

    QVERIFY(runnerCalled);
    QCOMPARE(observedTempPath, tempMp4Path);
    QVERIFY(!result.ok);
    QCOMPARE(result.code, QStringLiteral("timeout"));
    QVERIFY(result.message.contains(QStringLiteral("timed out")));
    QVERIFY(!QFileInfo::exists(finalPath));
    QVERIFY(!QFileInfo::exists(tempMp4Path));

    MediaFinalizerTestAdapter::clearTestProcessRunner(finalizer);
}

void CoreRegressionTests::mediaFinalizer_remuxCancel_reportsCancelledAndDoesNotPublishMp4()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString stagingPath = QDir(tempDir.path()).filePath("result.ts");
    QVERIFY(createFileWithContents(stagingPath, createRemuxableTsFixtureBytes()));
    QVERIFY(MediaContainerValidator::validateFile(stagingPath, MediaContainerType::MpegTs).ok);

    QTemporaryDir assetsDir;
    QVERIFY(assetsDir.isValid());
    QVERIFY(createEmptyFile(QDir(assetsDir.path()).filePath("ffmpeg.exe")));

    std::atomic_bool cancelRequested{ false };
    MediaFinalizer finalizer;
    MediaFinalizerTestAdapter::setTestDecryptAssetsDir(finalizer, assetsDir.path());
    MediaFinalizerTestAdapter::setTestProcessRunner(finalizer, [&](const FfmpegCliProcessRequest& request) -> FfmpegCliProcessResult {
        if (!createFileWithContents(request.arguments.last(), QByteArrayLiteral("partial mp4 bytes"))) {
            FfmpegCliProcessResult result;
            result.errorString = QStringLiteral("failed to create synthetic remux temp file");
            return result;
        }

        for (int i = 0; i < 20; ++i) {
            if (i == 2) {
                cancelRequested.store(true, std::memory_order_relaxed);
            }

            if (request.cancellationRequested && request.cancellationRequested()) {
                FfmpegCliProcessResult result;
                result.started = true;
                result.cancelled = true;
                return result;
            }

            QThread::msleep(5);
        }

        FfmpegCliProcessResult result;
        result.started = true;
        result.exitCode = 0;
        result.exitStatus = QProcess::NormalExit;
        return result;
    });

    const MediaFinalizeResult result = finalizer.finalize(stagingPath,
        QStringLiteral("取消Remux"),
        tempDir.path(),
        MediaContainerType::Mp4,
        [&cancelRequested]() { return cancelRequested.load(std::memory_order_relaxed); });

    QVERIFY(!result.ok);
    QCOMPARE(result.code, QStringLiteral("cancelled"));
    QCOMPARE(result.message, QStringLiteral("cancelled"));
    QVERIFY(!QFileInfo::exists(QDir(tempDir.path()).filePath("取消Remux.mp4")));
    QVERIFY(!QFileInfo::exists(QDir(tempDir.path()).filePath("取消Remux.mp4.tmp")));

    MediaFinalizerTestAdapter::clearTestProcessRunner(finalizer);
    MediaFinalizerTestAdapter::clearTestDecryptAssetsDir(finalizer);
}

void CoreRegressionTests::mediaFinalizer_remuxProcessFailure_reportsDiagnostic()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString stagingPath = QDir(tempDir.path()).filePath("result.ts");
    QVERIFY(createFakeTsFile(stagingPath, 4, 256));

    MediaFinalizer finalizer;
    MediaFinalizerTestAdapter::setTestProcessRunner(finalizer, [](const FfmpegCliProcessRequest&) {
        FfmpegCliProcessResult result;
        result.started = true;
        result.exitCode = 9;
        result.stderrText = QStringLiteral("synthetic ffmpeg failure");
        return result;
    });

    const MediaFinalizeResult result = finalizer.finalize(stagingPath,
        QStringLiteral("失败Remux"),
        tempDir.path(),
        MediaContainerType::Mp4);

    QVERIFY(!result.ok);
    QCOMPARE(result.code, QStringLiteral("process_failed"));
    QVERIFY(result.message.contains(QStringLiteral("synthetic ffmpeg failure")));
    QVERIFY(!QFileInfo::exists(QDir(tempDir.path()).filePath("失败Remux.mp4")));

    MediaFinalizerTestAdapter::clearTestProcessRunner(finalizer);
}

void CoreRegressionTests::mediaFinalizer_invalidRemuxedMp4_reportsValidationFailure()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString stagingPath = QDir(tempDir.path()).filePath("result.ts");
    QVERIFY(createFakeTsFile(stagingPath, 4, 256));

    MediaFinalizer finalizer;
    MediaFinalizerTestAdapter::setTestProcessRunner(finalizer, [](const FfmpegCliProcessRequest& request) {
        FfmpegCliProcessResult result;
        result.started = true;
        result.exitCode = 0;
        result.exitStatus = QProcess::NormalExit;

        QFile outputFile(request.arguments.last());
        if (outputFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QByteArray fakeTsPacket(188, char(0x47));
            outputFile.write(fakeTsPacket);
            outputFile.write(fakeTsPacket);
            outputFile.write(fakeTsPacket);
            outputFile.write(fakeTsPacket);
            outputFile.close();
        }

        return result;
    });

    const MediaFinalizeResult result = finalizer.finalize(stagingPath,
        QStringLiteral("假MP4"),
        tempDir.path(),
        MediaContainerType::Mp4);

    QVERIFY(!result.ok);
    QCOMPARE(result.code, QStringLiteral("invalid_remuxed_mp4"));
    QVERIFY(result.message.contains(QStringLiteral("validation failed")));
    QVERIFY(!QFileInfo::exists(QDir(tempDir.path()).filePath("假MP4.mp4")));
    QVERIFY(!QFileInfo::exists(QDir(tempDir.path()).filePath("假MP4.mp4.tmp")));

    MediaFinalizerTestAdapter::clearTestProcessRunner(finalizer);
}

void CoreRegressionTests::directFinalizeWorker_cancelDuringRemux_emitsCancelledAndDoesNotPublish()
{
    QTemporaryDir assetsDir;
    QVERIFY(assetsDir.isValid());
    QVERIFY(createEmptyFile(QDir(assetsDir.path()).filePath("ffmpeg.exe")));

    const QString title = QStringLiteral("direct-finalize-cancel-video");
    const QString taskDirPath = QDir(m_tempDir->path()).filePath(decryptTaskHash(title));
    QVERIFY(QDir().mkpath(taskDirPath));

    const QString stagingPath = QDir(taskDirPath).filePath(QStringLiteral("result.ts"));
    QVERIFY(createFileWithContents(stagingPath, createRemuxableTsFixtureBytes()));

    std::atomic_bool runnerStarted{ false };
    std::atomic_bool cancelRequested{ false };
    DirectMediaFinalizeResult finalizeResult;

    const auto runner = [&](const FfmpegCliProcessRequest& request) -> FfmpegCliProcessResult {
        runnerStarted.store(true, std::memory_order_relaxed);
        if (!createFileWithContents(request.arguments.last(), QByteArrayLiteral("partial mp4 bytes"))) {
            FfmpegCliProcessResult result;
            result.errorString = QStringLiteral("failed to create synthetic remux temp file");
            return result;
        }

        for (int i = 0; i < 200; ++i) {
            if (request.cancellationRequested && request.cancellationRequested()) {
                FfmpegCliProcessResult result;
                result.started = true;
                result.cancelled = true;
                return result;
            }

            QThread::msleep(5);
        }

        FfmpegCliProcessResult result;
        result.started = true;
        result.exitCode = 0;
        result.exitStatus = QProcess::NormalExit;
        return result;
    };

    std::thread finalizeThread([&]() {
        finalizeResult = finalizeDirectTsTask(title,
            m_tempDir->path(),
            true,
            QString(),
            [&cancelRequested]() {
                return cancelRequested.load(std::memory_order_relaxed);
            },
            runner,
            assetsDir.path());
    });

    QTRY_VERIFY_WITH_TIMEOUT(runnerStarted.load(std::memory_order_relaxed), 2000);
    cancelRequested.store(true, std::memory_order_relaxed);
    finalizeThread.join();

    QVERIFY(!QFileInfo::exists(QDir(m_tempDir->path()).filePath(QStringLiteral("direct-finalize-cancel-video.mp4"))));
    QVERIFY(!QFileInfo::exists(QDir(m_tempDir->path()).filePath(QStringLiteral("direct-finalize-cancel-video.mp4.tmp"))));
    QCOMPARE(finalizeResult.ok, false);
    QCOMPARE(finalizeResult.code, QStringLiteral("cancelled"));
    QCOMPARE(finalizeResult.message, QStringLiteral("cancelled"));
    QCOMPARE(finalizeResult.finalPath, QString());
}

void CoreRegressionTests::downloadJob_legalStateTransitions_acceptsExpectedSequence()
{
    // Forward progression: Created → Queued → ResolvingM3u8 → Downloading
    //   → Concatenating → Decrypting ─→ Completed
    //   → Concatenating → DirectFinalizing ─→ Completed
    QCOMPARE(isValidTransition(DownloadJobState::Created, DownloadJobState::Queued), true);
    QCOMPARE(isValidTransition(DownloadJobState::Queued, DownloadJobState::ResolvingM3u8), true);
    QCOMPARE(isValidTransition(DownloadJobState::ResolvingM3u8, DownloadJobState::Downloading), true);
    QCOMPARE(isValidTransition(DownloadJobState::Downloading, DownloadJobState::Concatenating), true);
    QCOMPARE(isValidTransition(DownloadJobState::Concatenating, DownloadJobState::Decrypting), true);
    QCOMPARE(isValidTransition(DownloadJobState::Concatenating, DownloadJobState::DirectFinalizing), true);
    QCOMPARE(isValidTransition(DownloadJobState::Decrypting, DownloadJobState::Completed), true);
    QCOMPARE(isValidTransition(DownloadJobState::DirectFinalizing, DownloadJobState::Completed), true);

    // Failure from any active state
    QCOMPARE(isValidTransition(DownloadJobState::ResolvingM3u8, DownloadJobState::Failed), true);
    QCOMPARE(isValidTransition(DownloadJobState::Downloading, DownloadJobState::Failed), true);
    QCOMPARE(isValidTransition(DownloadJobState::Concatenating, DownloadJobState::Failed), true);
    QCOMPARE(isValidTransition(DownloadJobState::Decrypting, DownloadJobState::Failed), true);
    QCOMPARE(isValidTransition(DownloadJobState::DirectFinalizing, DownloadJobState::Failed), true);

    // Cancellation from any non-terminal state
    QCOMPARE(isValidTransition(DownloadJobState::Created, DownloadJobState::Cancelled), true);
    QCOMPARE(isValidTransition(DownloadJobState::Queued, DownloadJobState::Cancelled), true);
    QCOMPARE(isValidTransition(DownloadJobState::ResolvingM3u8, DownloadJobState::Cancelled), true);
    QCOMPARE(isValidTransition(DownloadJobState::Downloading, DownloadJobState::Cancelled), true);
    QCOMPARE(isValidTransition(DownloadJobState::Concatenating, DownloadJobState::Cancelled), true);
    QCOMPARE(isValidTransition(DownloadJobState::Decrypting, DownloadJobState::Cancelled), true);
    QCOMPARE(isValidTransition(DownloadJobState::DirectFinalizing, DownloadJobState::Cancelled), true);
}

void CoreRegressionTests::downloadJob_illegalStateTransitions_rejectsInvalidSequences()
{
    // Cannot skip states
    QCOMPARE(isValidTransition(DownloadJobState::Created, DownloadJobState::ResolvingM3u8), false);
    QCOMPARE(isValidTransition(DownloadJobState::Created, DownloadJobState::Downloading), false);
    QCOMPARE(isValidTransition(DownloadJobState::Created, DownloadJobState::Completed), false);
    QCOMPARE(isValidTransition(DownloadJobState::Queued, DownloadJobState::Downloading), false);
    QCOMPARE(isValidTransition(DownloadJobState::Queued, DownloadJobState::Completed), false);
    QCOMPARE(isValidTransition(DownloadJobState::ResolvingM3u8, DownloadJobState::Completed), false);
    QCOMPARE(isValidTransition(DownloadJobState::Downloading, DownloadJobState::Completed), false);

    // Cannot transition from terminal states
    QCOMPARE(isValidTransition(DownloadJobState::Completed, DownloadJobState::Created), false);
    QCOMPARE(isValidTransition(DownloadJobState::Completed, DownloadJobState::Failed), false);
    QCOMPARE(isValidTransition(DownloadJobState::Failed, DownloadJobState::Created), false);
    QCOMPARE(isValidTransition(DownloadJobState::Failed, DownloadJobState::Queued), false);
    QCOMPARE(isValidTransition(DownloadJobState::Cancelled, DownloadJobState::Created), false);
    QCOMPARE(isValidTransition(DownloadJobState::Cancelled, DownloadJobState::Failed), false);

    // Cannot go backwards
    QCOMPARE(isValidTransition(DownloadJobState::ResolvingM3u8, DownloadJobState::Queued), false);
    QCOMPARE(isValidTransition(DownloadJobState::Downloading, DownloadJobState::ResolvingM3u8), false);
    QCOMPARE(isValidTransition(DownloadJobState::Concatenating, DownloadJobState::Downloading), false);
    QCOMPARE(isValidTransition(DownloadJobState::Decrypting, DownloadJobState::Concatenating), false);
    QCOMPARE(isValidTransition(DownloadJobState::DirectFinalizing, DownloadJobState::Concatenating), false);
}

void CoreRegressionTests::downloadJob_failurePolicy_classifiesVideoSpecificErrors_asSkipVideo()
{
    QCOMPARE(classifyFailurePolicy(DownloadErrorCategory::NetworkError), BatchFailurePolicy::SkipVideo);
    QCOMPARE(classifyFailurePolicy(DownloadErrorCategory::Timeout), BatchFailurePolicy::SkipVideo);
    QCOMPARE(classifyFailurePolicy(DownloadErrorCategory::ServerError), BatchFailurePolicy::SkipVideo);
    QCOMPARE(classifyFailurePolicy(DownloadErrorCategory::DecryptError), BatchFailurePolicy::SkipVideo);
    QCOMPARE(classifyFailurePolicy(DownloadErrorCategory::ValidationError), BatchFailurePolicy::SkipVideo);
    QCOMPARE(classifyFailurePolicy(DownloadErrorCategory::Cancelled), BatchFailurePolicy::SkipVideo);
}

void CoreRegressionTests::downloadJob_failurePolicy_classifiesSharedEnvironmentErrors_asStopBatch()
{
    QCOMPARE(classifyFailurePolicy(DownloadErrorCategory::FileSystemError), BatchFailurePolicy::StopBatch);
    QCOMPARE(classifyFailurePolicy(DownloadErrorCategory::Unknown), BatchFailurePolicy::StopBatch);
}

void CoreRegressionTests::coordinatorFakeResolveService_supportsSuccessFailureAndCancel()
{
    FakeCoordinatorResolveService resolver;
    QSignalSpy resolvedSpy(&resolver, &CoordinatorResolveService::resolved);
    QSignalSpy failedSpy(&resolver, &CoordinatorResolveService::failed);
    QSignalSpy cancelledSpy(&resolver, &CoordinatorResolveService::cancelled);

    resolver.queueSuccess({QStringLiteral("https://fake.test/0001.ts"), QStringLiteral("https://fake.test/0002.ts")}, true);
    resolver.startResolve(QStringLiteral("guid-success"), QStringLiteral("4K"));
    QVERIFY(resolvedSpy.wait(1000));
    QCOMPARE(resolvedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(cancelledSpy.count(), 0);
    QCOMPARE(resolver.lastGuid(), QStringLiteral("guid-success"));
    QCOMPARE(resolver.lastQuality(), QStringLiteral("4K"));
    const auto resolvedArgs = resolvedSpy.takeFirst();
    QCOMPARE(resolvedArgs.at(1).toBool(), true);
    QCOMPARE(resolvedArgs.at(0).toStringList().size(), 2);

    resolver.queueFailure(DownloadErrorCategory::NetworkError, QStringLiteral("synthetic network failure"));
    resolver.startResolve(QStringLiteral("guid-failure"), QStringLiteral("1080P"));
    QVERIFY(failedSpy.wait(1000));
    QCOMPARE(failedSpy.count(), 1);
    const auto failedArgs = failedSpy.takeFirst();
    QCOMPARE(qvariant_cast<DownloadErrorCategory>(failedArgs.at(0)), DownloadErrorCategory::NetworkError);
    QCOMPARE(failedArgs.at(1).toString(), QStringLiteral("synthetic network failure"));

    resolver.queueSuccess({QStringLiteral("https://fake.test/cancelled.ts")}, false);
    resolver.startResolve(QStringLiteral("guid-cancel"), QStringLiteral("720P"));
    QVERIFY(cancelledSpy.isEmpty());
    resolver.cancelResolve();
    QCOMPARE(cancelledSpy.count(), 1);
    QTest::qWait(20);
    QCOMPARE(resolvedSpy.count(), 0);
}

void CoreRegressionTests::coordinatorFakeDownloadStage_supportsProgressFailureAndCancel()
{
    FakeCoordinatorDownloadStage stage;
    QSignalSpy progressSpy(&stage, &CoordinatorDownloadStage::downloadProgress);
    QSignalSpy finishedSpy(&stage, &CoordinatorDownloadStage::downloadFinished);
    QSignalSpy allFinishedSpy(&stage, &CoordinatorDownloadStage::allDownloadFinished);

    const QStringList segmentUrls = {QStringLiteral("https://fake.test/0001.ts"), QStringLiteral("https://fake.test/0002.ts")};
    const QVariant successUserData(QStringLiteral("job-success"));

    stage.queueSuccess({{32, 100}, {100, 100}});
    stage.startDownload(segmentUrls, QStringLiteral("C:/fake/save"), successUserData);
    QVERIFY(finishedSpy.wait(1000));
    QCOMPARE(stage.lastUrls(), segmentUrls);
    QCOMPARE(stage.lastSaveDir(), QStringLiteral("C:/fake/save"));
    QCOMPARE(progressSpy.count(), 2);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(allFinishedSpy.count(), 1);
    auto successArgs = finishedSpy.takeFirst();
    QCOMPARE(successArgs.at(0).toBool(), true);
    QCOMPARE(successArgs.at(1).toString(), QString());
    QCOMPARE(successArgs.at(2), successUserData);

    const QVariant failureUserData(QStringLiteral("job-failure"));
    stage.queueFailure({{10, 100}}, QStringLiteral("synthetic shard failure"));
    stage.startDownload(segmentUrls, QStringLiteral("C:/fake/save"), failureUserData);
    QVERIFY(finishedSpy.wait(1000));
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(allFinishedSpy.count(), 2);
    auto failureArgs = finishedSpy.takeFirst();
    QCOMPARE(failureArgs.at(0).toBool(), false);
    QCOMPARE(failureArgs.at(1).toString(), QStringLiteral("synthetic shard failure"));
    QCOMPARE(failureArgs.at(2), failureUserData);

    const QVariant cancelledUserData(QStringLiteral("job-cancel"));
    stage.queueSuccess({{5, 100}, {50, 100}});
    stage.startDownload(segmentUrls, QStringLiteral("C:/fake/save"), cancelledUserData);
    stage.cancelDownload(cancelledUserData);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(allFinishedSpy.count(), 3);
    auto cancelledArgs = finishedSpy.takeFirst();
    QCOMPARE(cancelledArgs.at(0).toBool(), false);
    QCOMPARE(cancelledArgs.at(1).toString(), QStringLiteral("cancelled"));
    QCOMPARE(cancelledArgs.at(2), cancelledUserData);
    QTest::qWait(20);
    QCOMPARE(finishedSpy.count(), 0);
}

void CoreRegressionTests::coordinatorFakeConcatStage_supportsSuccessFailureAndCancel()
{
    FakeCoordinatorConcatStage stage;
    QSignalSpy spy(&stage, &CoordinatorConcatStage::concatFinished);

    stage.setFilePath(QStringLiteral("C:/fake/task"));
    QCOMPARE(stage.filePath(), QStringLiteral("C:/fake/task"));

    stage.queueSuccess(QStringLiteral("result.ts ready"));
    stage.startConcat();
    QVERIFY(spy.wait(1000));
    QCOMPARE(spy.count(), 1);
    auto successArgs = spy.takeFirst();
    QCOMPARE(successArgs.at(0).toBool(), true);
    QCOMPARE(successArgs.at(1).toString(), QStringLiteral("result.ts ready"));

    stage.queueFailure(QStringLiteral("synthetic concat failure"));
    stage.startConcat();
    QVERIFY(spy.wait(1000));
    QCOMPARE(spy.count(), 1);
    auto failureArgs = spy.takeFirst();
    QCOMPARE(failureArgs.at(0).toBool(), false);
    QCOMPARE(failureArgs.at(1).toString(), QStringLiteral("synthetic concat failure"));

    stage.queueSuccess(QStringLiteral("would have succeeded"));
    stage.startConcat();
    stage.cancelConcat();
    QCOMPARE(spy.count(), 1);
    auto cancelledArgs = spy.takeFirst();
    QCOMPARE(cancelledArgs.at(0).toBool(), false);
    QCOMPARE(cancelledArgs.at(1).toString(), QStringLiteral("cancelled"));
    QTest::qWait(20);
    QCOMPARE(spy.count(), 0);
}

void CoreRegressionTests::coordinatorFakeDecryptStage_supportsSuccessFailureAndCancel()
{
    FakeCoordinatorDecryptStage stage;
    QSignalSpy spy(&stage, &CoordinatorDecryptStage::decryptFinished);

    stage.setParams(QStringLiteral("节目A"), QStringLiteral("C:/fake/save"));
    stage.setTranscodeToMp4(true);
    QCOMPARE(stage.name(), QStringLiteral("节目A"));
    QCOMPARE(stage.savePath(), QStringLiteral("C:/fake/save"));
    QCOMPARE(stage.transcodeToMp4(), true);

    stage.queueSuccess(QStringLiteral("decrypt ok"));
    stage.startDecrypt();
    QVERIFY(spy.wait(1000));
    QCOMPARE(spy.count(), 1);
    auto successArgs = spy.takeFirst();
    QCOMPARE(successArgs.at(0).toBool(), true);
    QCOMPARE(successArgs.at(1).toString(), QStringLiteral("decrypt ok"));

    stage.queueFailure(QStringLiteral("synthetic decrypt failure"));
    stage.startDecrypt();
    QVERIFY(spy.wait(1000));
    auto failureArgs = spy.takeFirst();
    QCOMPARE(failureArgs.at(0).toBool(), false);
    QCOMPARE(failureArgs.at(1).toString(), QStringLiteral("synthetic decrypt failure"));

    stage.queueSuccess(QStringLiteral("would have succeeded"));
    stage.startDecrypt();
    stage.cancelDecrypt();
    QCOMPARE(spy.count(), 1);
    auto cancelledArgs = spy.takeFirst();
    QCOMPARE(cancelledArgs.at(0).toBool(), false);
    QCOMPARE(cancelledArgs.at(1).toString(), QStringLiteral("cancelled"));
    QTest::qWait(20);
    QCOMPARE(spy.count(), 0);
}

void CoreRegressionTests::coordinatorFakeDirectFinalizeStage_supportsSuccessFailureAndCancel()
{
    FakeCoordinatorDirectFinalizeStage stage;
    QSignalSpy spy(&stage, &CoordinatorDirectFinalizeStage::finished);

    stage.queueSuccess(QStringLiteral("published_mp4"), QStringLiteral("finalized"), QStringLiteral("C:/fake/output.mp4"));
    stage.startFinalize(QStringLiteral("节目B"), QStringLiteral("C:/fake/save"), true);
    QVERIFY(spy.wait(1000));
    QCOMPARE(stage.title(), QStringLiteral("节目B"));
    QCOMPARE(stage.savePath(), QStringLiteral("C:/fake/save"));
    QCOMPARE(stage.transcodeToMp4(), true);
    auto successArgs = spy.takeFirst();
    QCOMPARE(successArgs.at(0).toBool(), true);
    QCOMPARE(successArgs.at(1).toString(), QStringLiteral("published_mp4"));
    QCOMPARE(successArgs.at(2).toString(), QStringLiteral("finalized"));
    QCOMPARE(successArgs.at(3).toString(), QStringLiteral("C:/fake/output.mp4"));

    stage.queueFailure(QStringLiteral("ffmpeg_missing"), QStringLiteral("synthetic ffmpeg missing"));
    stage.startFinalize(QStringLiteral("节目C"), QStringLiteral("C:/fake/save"), false);
    QVERIFY(spy.wait(1000));
    auto failureArgs = spy.takeFirst();
    QCOMPARE(failureArgs.at(0).toBool(), false);
    QCOMPARE(failureArgs.at(1).toString(), QStringLiteral("ffmpeg_missing"));
    QCOMPARE(failureArgs.at(2).toString(), QStringLiteral("synthetic ffmpeg missing"));
    QCOMPARE(failureArgs.at(3).toString(), QString());

    stage.queueSuccess(QStringLiteral("would_publish"), QStringLiteral("would finalize"), QStringLiteral("C:/fake/would-not-exist.mp4"));
    stage.startFinalize(QStringLiteral("节目D"), QStringLiteral("C:/fake/save"), false);
    stage.cancelFinalize();
    QCOMPARE(spy.count(), 1);
    auto cancelledArgs = spy.takeFirst();
    QCOMPARE(cancelledArgs.at(0).toBool(), false);
    QCOMPARE(cancelledArgs.at(1).toString(), QStringLiteral("cancelled"));
    QCOMPARE(cancelledArgs.at(2).toString(), QStringLiteral("cancelled"));
    QCOMPARE(cancelledArgs.at(3).toString(), QString());
    QTest::qWait(20);
    QCOMPARE(spy.count(), 0);
}

void CoreRegressionTests::downloadCoordinator_batchSuccess_processesJobsInOrder()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    resolver.queueSuccess({QStringLiteral("https://fake.test/a-1.ts")}, false);
    resolver.queueSuccess({QStringLiteral("https://fake.test/b-1.ts")}, true);
    resolver.queueSuccess({QStringLiteral("https://fake.test/c-1.ts")}, false);
    downloadStage.queueSuccess({{50, 100}, {100, 100}});
    downloadStage.queueSuccess({{100, 100}});
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-a"));
    concatStage.queueSuccess(QStringLiteral("concat-b"));
    concatStage.queueSuccess(QStringLiteral("concat-c"));
    decryptStage.queueSuccess(QStringLiteral("decrypt-a"));
    decryptStage.queueSuccess(QStringLiteral("decrypt-c"));
    directFinalizeStage.queueSuccess(QStringLiteral("published_ts"), QStringLiteral("finalize-b"), QStringLiteral("C:/fake/b.ts"));

    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    const QList<DownloadJob> jobs = {
        makeCoordinatorJob(QStringLiteral("job-a"), QStringLiteral("guid-a"), QStringLiteral("节目A"), QStringLiteral("1080P"), QStringLiteral("C:/fake/a")),
        makeCoordinatorJob(QStringLiteral("job-b"), QStringLiteral("guid-b"), QStringLiteral("节目B"), QStringLiteral("4K"), QStringLiteral("C:/fake/b")),
        makeCoordinatorJob(QStringLiteral("job-c"), QStringLiteral("guid-c"), QStringLiteral("节目C"), QStringLiteral("720P"), QStringLiteral("C:/fake/c"))
    };

    QVERIFY(coordinator.startBatch(jobs));
    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 1000);
    QCOMPARE(jobFinishedSpy.count(), 3);

    const auto firstFinished = qvariant_cast<DownloadJob>(jobFinishedSpy.at(0).at(0));
    const auto secondFinished = qvariant_cast<DownloadJob>(jobFinishedSpy.at(1).at(0));
    const auto thirdFinished = qvariant_cast<DownloadJob>(jobFinishedSpy.at(2).at(0));
    QCOMPARE(firstFinished.id, QStringLiteral("job-a"));
    QCOMPARE(secondFinished.id, QStringLiteral("job-b"));
    QCOMPARE(thirdFinished.id, QStringLiteral("job-c"));
    QCOMPARE(firstFinished.state, DownloadJobState::Completed);
    QCOMPARE(secondFinished.state, DownloadJobState::Completed);
    QCOMPARE(thirdFinished.state, DownloadJobState::Completed);

    const auto batchArgs = batchFinishedSpy.takeFirst();
    QCOMPARE(batchArgs.at(0).toInt(), 3);
    QCOMPARE(batchArgs.at(1).toInt(), 0);
    QCOMPARE(batchArgs.at(2).toInt(), 0);
    QCOMPARE(batchArgs.at(3).toInt(), 3);
    QCOMPARE(batchArgs.at(4).toBool(), false);
    QCOMPARE(coordinator.completedJobs(), 3);
    QCOMPARE(coordinator.failedJobs(), 0);
    QCOMPARE(coordinator.cancelledJobs(), 0);
}

void CoreRegressionTests::downloadCoordinator_normalJob_emitsConcatThenDecryptSequence()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    resolver.queueSuccess({QStringLiteral("https://fake.test/normal-1.ts")}, false);
    downloadStage.queueSuccess({{25, 100}, {100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-normal"));
    decryptStage.queueSuccess(QStringLiteral("decrypt-normal"));

    QSignalSpy jobChangedSpy(&coordinator, &DownloadCoordinator::jobChanged);
    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startSingle(makeCoordinatorJob(QStringLiteral("job-normal-sequence"),
        QStringLiteral("guid-normal-sequence"),
        QStringLiteral("节目普通流程"),
        QStringLiteral("1080P"),
        QStringLiteral("C:/fake/normal-sequence"))));

    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 1000);
    QCOMPARE(jobFinishedSpy.count(), 1);

    QList<DownloadJobState> states;
    for (const QList<QVariant>& emission : jobChangedSpy) {
        const DownloadJob job = qvariant_cast<DownloadJob>(emission.at(0));
        if (job.id != QStringLiteral("job-normal-sequence")) {
            continue;
        }
        if (states.isEmpty() || states.constLast() != job.state) {
            states.append(job.state);
        }
    }

    QCOMPARE(states, QList<DownloadJobState>({
        DownloadJobState::Queued,
        DownloadJobState::ResolvingM3u8,
        DownloadJobState::Downloading,
        DownloadJobState::Concatenating,
        DownloadJobState::Decrypting,
        DownloadJobState::Completed
    }));
}

void CoreRegressionTests::downloadCoordinator_4kJob_emitsConcatThenDirectFinalizeSequence()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    resolver.queueSuccess({QStringLiteral("https://fake.test/4k-1.ts")}, true);
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-4k"));
    directFinalizeStage.queueSuccess(QStringLiteral("published_ts"), QStringLiteral("finalize-4k"), QStringLiteral("C:/fake/4k.ts"));

    QSignalSpy jobChangedSpy(&coordinator, &DownloadCoordinator::jobChanged);
    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startSingle(makeCoordinatorJob(QStringLiteral("job-4k-sequence"),
        QStringLiteral("guid-4k-sequence"),
        QStringLiteral("节目4K流程"),
        QStringLiteral("4K"),
        QStringLiteral("C:/fake/4k-sequence"))));

    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 1000);
    QCOMPARE(jobFinishedSpy.count(), 1);

    QList<DownloadJobState> states;
    for (const QList<QVariant>& emission : jobChangedSpy) {
        const DownloadJob job = qvariant_cast<DownloadJob>(emission.at(0));
        if (job.id != QStringLiteral("job-4k-sequence")) {
            continue;
        }
        if (states.isEmpty() || states.constLast() != job.state) {
            states.append(job.state);
        }
    }

    QCOMPARE(states, QList<DownloadJobState>({
        DownloadJobState::Queued,
        DownloadJobState::ResolvingM3u8,
        DownloadJobState::Downloading,
        DownloadJobState::Concatenating,
        DownloadJobState::DirectFinalizing,
        DownloadJobState::Completed
    }));
}

void CoreRegressionTests::downloadCoordinator_busyCoordinator_rejectsSecondBatch()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    resolver.queueSuccess({QStringLiteral("https://fake.test/only.ts")}, false);
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat"));
    decryptStage.queueSuccess(QStringLiteral("decrypt"));

    QSignalSpy busySpy(&coordinator, &DownloadCoordinator::batchBusy);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startSingle(makeCoordinatorJob(QStringLiteral("job-1"), QStringLiteral("guid-1"), QStringLiteral("节目1"), QStringLiteral("1080P"), QStringLiteral("C:/fake/1"))));
    QVERIFY(!coordinator.startSingle(makeCoordinatorJob(QStringLiteral("job-2"), QStringLiteral("guid-2"), QStringLiteral("节目2"), QStringLiteral("1080P"), QStringLiteral("C:/fake/2"))));
    QCOMPARE(busySpy.count(), 1);
    QVERIFY(batchFinishedSpy.wait(1000));
    QCOMPARE(batchFinishedSpy.count(), 1);
    QCOMPARE(coordinator.completedJobs(), 1);
}

void CoreRegressionTests::downloadCoordinator_duplicateJobs_processesEachSelectionIndependently()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    resolver.queueSuccess({QStringLiteral("https://fake.test/dup-1.ts")}, false);
    resolver.queueSuccess({QStringLiteral("https://fake.test/dup-2.ts")}, false);
    downloadStage.queueSuccess({{100, 100}});
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-dup-1"));
    concatStage.queueSuccess(QStringLiteral("concat-dup-2"));
    decryptStage.queueSuccess(QStringLiteral("decrypt-dup-1"));
    decryptStage.queueSuccess(QStringLiteral("decrypt-dup-2"));

    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    const DownloadJob duplicateJob = makeCoordinatorJob(QStringLiteral("duplicate-job"),
        QStringLiteral("duplicate-guid"),
        QStringLiteral("重复视频"),
        QStringLiteral("1080P"),
        QStringLiteral("C:/fake/duplicate"));

    QVERIFY(coordinator.startBatch({ duplicateJob, duplicateJob }));

    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 1000);
    QCOMPARE(jobFinishedSpy.count(), 2);
    QCOMPARE(coordinator.completedJobs(), 2);

    const auto firstJob = qvariant_cast<DownloadJob>(jobFinishedSpy.at(0).at(0));
    const auto secondJob = qvariant_cast<DownloadJob>(jobFinishedSpy.at(1).at(0));
    QCOMPARE(firstJob.id, QStringLiteral("duplicate-job"));
    QCOMPARE(secondJob.id, QStringLiteral("duplicate-job"));
    QCOMPARE(firstJob.request.videoTitle, QStringLiteral("重复视频"));
    QCOMPARE(secondJob.request.videoTitle, QStringLiteral("重复视频"));
    QCOMPARE(firstJob.state, DownloadJobState::Completed);
    QCOMPARE(secondJob.state, DownloadJobState::Completed);
}

void CoreRegressionTests::downloadCoordinator_secondBatchWhileActive_emitsBusyAndDoesNotStart()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    resolver.queueSuccess({QStringLiteral("https://fake.test/active-1.ts")}, false);
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-active-1"), 50);
    decryptStage.queueSuccess(QStringLiteral("decrypt-active-1"));

    QSignalSpy busySpy(&coordinator, &DownloadCoordinator::batchBusy);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);
    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);

    QVERIFY(coordinator.startBatch({
        makeCoordinatorJob(QStringLiteral("job-active-1"), QStringLiteral("guid-active-1"), QStringLiteral("活动批次A"), QStringLiteral("1080P"), QStringLiteral("C:/fake/active-a"))
    }));

    QVERIFY(!coordinator.startBatch({
        makeCoordinatorJob(QStringLiteral("job-active-2"), QStringLiteral("guid-active-2"), QStringLiteral("活动批次B"), QStringLiteral("720P"), QStringLiteral("C:/fake/active-b")),
        makeCoordinatorJob(QStringLiteral("job-active-3"), QStringLiteral("guid-active-3"), QStringLiteral("活动批次C"), QStringLiteral("720P"), QStringLiteral("C:/fake/active-c"))
    }));

    QCOMPARE(busySpy.count(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 1000);
    QCOMPARE(jobFinishedSpy.count(), 1);

    const auto finishedJob = qvariant_cast<DownloadJob>(jobFinishedSpy.takeFirst().at(0));
    QCOMPARE(finishedJob.id, QStringLiteral("job-active-1"));
    QCOMPARE(finishedJob.state, DownloadJobState::Completed);
}

void CoreRegressionTests::downloadCoordinator_videoSpecificFailure_advancesToNextJob()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    resolver.queueSuccess({QStringLiteral("https://fake.test/a.ts")}, false);
    resolver.queueSuccess({QStringLiteral("https://fake.test/b.ts")}, false);
    downloadStage.queueSuccess({{100, 100}});
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-a"));
    concatStage.queueSuccess(QStringLiteral("concat-b"));
    decryptStage.queueFailure(QStringLiteral("synthetic decrypt failure"));
    decryptStage.queueSuccess(QStringLiteral("decrypt-b"));

    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startBatch({
        makeCoordinatorJob(QStringLiteral("job-a"), QStringLiteral("guid-a"), QStringLiteral("节目A"), QStringLiteral("1080P"), QStringLiteral("C:/fake/a")),
        makeCoordinatorJob(QStringLiteral("job-b"), QStringLiteral("guid-b"), QStringLiteral("节目B"), QStringLiteral("720P"), QStringLiteral("C:/fake/b"))
    }));

    QVERIFY(batchFinishedSpy.wait(1000));
    QCOMPARE(jobFinishedSpy.count(), 2);

    const auto failedJob = qvariant_cast<DownloadJob>(jobFinishedSpy.at(0).at(0));
    const auto completedJob = qvariant_cast<DownloadJob>(jobFinishedSpy.at(1).at(0));
    QCOMPARE(failedJob.id, QStringLiteral("job-a"));
    QCOMPARE(failedJob.state, DownloadJobState::Failed);
    QCOMPARE(failedJob.errorCategory, DownloadErrorCategory::DecryptError);
    QCOMPARE(completedJob.id, QStringLiteral("job-b"));
    QCOMPARE(completedJob.state, DownloadJobState::Completed);

    const auto batchArgs = batchFinishedSpy.takeFirst();
    QCOMPARE(batchArgs.at(0).toInt(), 1);
    QCOMPARE(batchArgs.at(1).toInt(), 1);
    QCOMPARE(batchArgs.at(2).toInt(), 0);
    QCOMPARE(batchArgs.at(3).toInt(), 2);
    QCOMPARE(batchArgs.at(4).toBool(), false);
}

void CoreRegressionTests::downloadCoordinator_sharedEnvironmentFailure_stopsBatch()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    resolver.queueSuccess({QStringLiteral("https://fake.test/a.ts")}, true);
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-a"));
    directFinalizeStage.queueFailure(QStringLiteral("ffmpeg_missing"), QStringLiteral("synthetic ffmpeg missing"));

    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy fatalSpy(&coordinator, &DownloadCoordinator::fatalBatchFailure);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startBatch({
        makeCoordinatorJob(QStringLiteral("job-a"), QStringLiteral("guid-a"), QStringLiteral("节目A"), QStringLiteral("4K"), QStringLiteral("C:/fake/a")),
        makeCoordinatorJob(QStringLiteral("job-b"), QStringLiteral("guid-b"), QStringLiteral("节目B"), QStringLiteral("1080P"), QStringLiteral("C:/fake/b")),
        makeCoordinatorJob(QStringLiteral("job-c"), QStringLiteral("guid-c"), QStringLiteral("节目C"), QStringLiteral("720P"), QStringLiteral("C:/fake/c"))
    }));

    QVERIFY(batchFinishedSpy.wait(1000));
    QCOMPARE(fatalSpy.count(), 1);
    QCOMPARE(jobFinishedSpy.count(), 3);

    const auto firstJob = qvariant_cast<DownloadJob>(jobFinishedSpy.at(0).at(0));
    const auto secondJob = qvariant_cast<DownloadJob>(jobFinishedSpy.at(1).at(0));
    const auto thirdJob = qvariant_cast<DownloadJob>(jobFinishedSpy.at(2).at(0));
    QCOMPARE(firstJob.id, QStringLiteral("job-a"));
    QCOMPARE(firstJob.state, DownloadJobState::Failed);
    QCOMPARE(firstJob.errorCategory, DownloadErrorCategory::FileSystemError);
    QCOMPARE(secondJob.state, DownloadJobState::Cancelled);
    QCOMPARE(thirdJob.state, DownloadJobState::Cancelled);

    const auto batchArgs = batchFinishedSpy.takeFirst();
    QCOMPARE(batchArgs.at(0).toInt(), 0);
    QCOMPARE(batchArgs.at(1).toInt(), 1);
    QCOMPARE(batchArgs.at(2).toInt(), 2);
    QCOMPARE(batchArgs.at(3).toInt(), 3);
    QCOMPARE(batchArgs.at(4).toBool(), true);
}

void CoreRegressionTests::downloadCoordinator_apiServiceResolveSuccess_startsDownloadFromAsyncSignals()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&apiService, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    const QString guid = QStringLiteral("coordinator-api-success-guid");
    QUrl infoUrl(QStringLiteral("https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do"));
    QUrlQuery infoQuery;
    infoQuery.addQueryItem(QStringLiteral("pid"), guid);
    infoUrl.setQuery(infoQuery);
    manager.queueSuccess(infoUrl, QByteArray(R"({"manifest":{"hls_enc2_url":"https://media.example/asp/enc2/master.m3u8"}})"));
    manager.queueSuccess(QUrl(QStringLiteral("https://drm.cntv.vod.dnsv1.com/asp/enc2/master.m3u8")),
        QByteArray("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1228800\n/video/720/index.m3u8\n"));
    manager.queueSuccess(QUrl(QStringLiteral("https://drm.cntv.vod.dnsv1.com/video/720/index.m3u8")),
        QByteArray("#EXTM3U\n#EXTINF:2.0,\n0001.ts\n#EXTINF:2.0,\n0002.ts\n"));
    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);

    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-success"));
    decryptStage.queueSuccess(QStringLiteral("decrypt-success"));

    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startSingle(makeCoordinatorJob(QStringLiteral("job-api-success"), guid, QStringLiteral("节目API成功"), QStringLiteral("2"), QStringLiteral("C:/fake/api-success"))));
    QVERIFY(batchFinishedSpy.wait(1000));
    QCOMPARE(jobFinishedSpy.count(), 1);
    QCOMPARE(downloadStage.lastUrls(), QStringList({
        QStringLiteral("https://drm.cntv.vod.dnsv1.com/video/720/0001.ts"),
        QStringLiteral("https://drm.cntv.vod.dnsv1.com/video/720/0002.ts")
    }));
    QCOMPARE(downloadStage.lastSaveDir(),
        QDir(QStringLiteral("C:/fake/api-success")).filePath(coordinatorTaskHash(QStringLiteral("节目API成功"), QStringLiteral("job-api-success"))));

    const auto finishedJob = qvariant_cast<DownloadJob>(jobFinishedSpy.takeFirst().at(0));
    QCOMPARE(finishedJob.state, DownloadJobState::Completed);
    QCOMPARE(manager.requestCount(), 3);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::downloadCoordinator_apiServiceResolveFailure_isVideoSpecificAndAdvances()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&apiService, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    const QString failedGuid = QStringLiteral("coordinator-api-failure-guid");
    QUrl failedInfoUrl(QStringLiteral("https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do"));
    QUrlQuery failedInfoQuery;
    failedInfoQuery.addQueryItem(QStringLiteral("pid"), failedGuid);
    failedInfoUrl.setQuery(failedInfoQuery);
    manager.queueSuccess(failedInfoUrl, QByteArray(R"({"manifest":{}})"));

    const QString successGuid = QStringLiteral("coordinator-api-next-guid");
    QUrl successInfoUrl(QStringLiteral("https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do"));
    QUrlQuery successInfoQuery;
    successInfoQuery.addQueryItem(QStringLiteral("pid"), successGuid);
    successInfoUrl.setQuery(successInfoQuery);
    manager.queueSuccess(successInfoUrl, QByteArray(R"({"play_channel":"CCTV-4K","hls_url":"https://4k.example/live/main/index.m3u8"})"));
    manager.queueSuccess(QUrl(QStringLiteral("https://4k.example/live/4000/index.m3u8")),
        QByteArray("#EXTM3U\r\n#EXTINF:2.0,\r\n0.ts?maxbr=2048\r\n#EXTINF:2.0,\r\n1.ts?maxbr=2048\r\n"));
    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);

    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-next"));
    directFinalizeStage.queueSuccess(QStringLiteral("published_ts"), QStringLiteral("finalized"), QStringLiteral("C:/fake/api-next.ts"));

    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startBatch({
        makeCoordinatorJob(QStringLiteral("job-api-failed"), failedGuid, QStringLiteral("节目API失败"), QStringLiteral("0"), QStringLiteral("C:/fake/api-failed")),
        makeCoordinatorJob(QStringLiteral("job-api-next"), successGuid, QStringLiteral("节目API后续"), QStringLiteral("0"), QStringLiteral("C:/fake/api-next"))
    }));

    QVERIFY(batchFinishedSpy.wait(1000));
    QCOMPARE(jobFinishedSpy.count(), 2);

    const auto failedJob = qvariant_cast<DownloadJob>(jobFinishedSpy.at(0).at(0));
    const auto completedJob = qvariant_cast<DownloadJob>(jobFinishedSpy.at(1).at(0));
    QCOMPARE(failedJob.id, QStringLiteral("job-api-failed"));
    QCOMPARE(failedJob.state, DownloadJobState::Failed);
    QCOMPARE(failedJob.errorCategory, DownloadErrorCategory::ValidationError);
    QCOMPARE(failedJob.errorMessage, QStringLiteral("无法获取hls_enc2_url"));
    QCOMPARE(completedJob.id, QStringLiteral("job-api-next"));
    QCOMPARE(completedJob.state, DownloadJobState::Completed);
    QCOMPARE(manager.requestCount(), 3);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::downloadCoordinator_apiServiceMalformedInfoResponse_isValidationFailureAndAdvances()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&apiService, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    const QString malformedGuid = QStringLiteral("coordinator-api-malformed-guid");
    QUrl malformedInfoUrl(QStringLiteral("https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do"));
    QUrlQuery malformedInfoQuery;
    malformedInfoQuery.addQueryItem(QStringLiteral("pid"), malformedGuid);
    malformedInfoUrl.setQuery(malformedInfoQuery);
    manager.queueSuccess(malformedInfoUrl, QByteArray("not-json-response"));

    const QString successGuid = QStringLiteral("coordinator-api-malformed-next-guid");
    QUrl successInfoUrl(QStringLiteral("https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do"));
    QUrlQuery successInfoQuery;
    successInfoQuery.addQueryItem(QStringLiteral("pid"), successGuid);
    successInfoUrl.setQuery(successInfoQuery);
    manager.queueSuccess(successInfoUrl, QByteArray(R"({"manifest":{"hls_enc2_url":"https://media.example/asp/enc2/master.m3u8"}})"));
    manager.queueSuccess(QUrl(QStringLiteral("https://drm.cntv.vod.dnsv1.com/asp/enc2/master.m3u8")),
        QByteArray("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1228800\n/video/720/index.m3u8\n"));
    manager.queueSuccess(QUrl(QStringLiteral("https://drm.cntv.vod.dnsv1.com/video/720/index.m3u8")),
        QByteArray("#EXTM3U\n#EXTINF:2.0,\n0001.ts\n"));
    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);

    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-malformed-next"));
    decryptStage.queueSuccess(QStringLiteral("decrypt-malformed-next"));

    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startBatch({
        makeCoordinatorJob(QStringLiteral("job-api-malformed"), malformedGuid, QStringLiteral("节目API异常"), QStringLiteral("0"), QStringLiteral("C:/fake/api-malformed")),
        makeCoordinatorJob(QStringLiteral("job-api-malformed-next"), successGuid, QStringLiteral("节目API后续成功"), QStringLiteral("2"), QStringLiteral("C:/fake/api-malformed-next"))
    }));

    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 1000);
    QCOMPARE(jobFinishedSpy.count(), 2);

    const auto failedJob = qvariant_cast<DownloadJob>(jobFinishedSpy.at(0).at(0));
    const auto completedJob = qvariant_cast<DownloadJob>(jobFinishedSpy.at(1).at(0));
    QCOMPARE(failedJob.id, QStringLiteral("job-api-malformed"));
    QCOMPARE(failedJob.state, DownloadJobState::Failed);
    QCOMPARE(failedJob.errorCategory, DownloadErrorCategory::ValidationError);
    QCOMPARE(failedJob.errorMessage, QStringLiteral("无法获取hls_enc2_url"));
    QCOMPARE(completedJob.id, QStringLiteral("job-api-malformed-next"));
    QCOMPARE(completedJob.state, DownloadJobState::Completed);
    QCOMPARE(manager.requestCount(), 4);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::downloadCoordinator_cancelCurrentWhileResolving_apiRequestAbortsAndNoDownloadStarts()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&apiService, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    const QString guid = QStringLiteral("coordinator-api-cancel-guid");
    QUrl infoUrl(QStringLiteral("https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do"));
    QUrlQuery infoQuery;
    infoQuery.addQueryItem(QStringLiteral("pid"), guid);
    infoUrl.setQuery(infoQuery);
    manager.queueSuccess(infoUrl, QByteArray(R"({"play_channel":"CCTV-4K","hls_url":"https://4k.example/live/main/index.m3u8"})"), 200);
    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);

    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startSingle(makeCoordinatorJob(QStringLiteral("job-api-cancel"), guid, QStringLiteral("节目API取消"), QStringLiteral("0"), QStringLiteral("C:/fake/api-cancel"))));

    QTRY_VERIFY_WITH_TIMEOUT(manager.lastReply() != nullptr, 1000);
    FakeNetworkReply* pendingReply = manager.lastReply();
    QVERIFY(pendingReply != nullptr);

    coordinator.cancelCurrent();

    QVERIFY(batchFinishedSpy.wait(1000));
    QCOMPARE(jobFinishedSpy.count(), 1);
    QVERIFY(pendingReply->wasAborted());
    QCOMPARE(downloadStage.lastUrls(), QStringList());
    QCOMPARE(downloadStage.lastSaveDir(), QString());

    const auto cancelledJob = qvariant_cast<DownloadJob>(jobFinishedSpy.takeFirst().at(0));
    QCOMPARE(cancelledJob.id, QStringLiteral("job-api-cancel"));
    QCOMPARE(cancelledJob.state, DownloadJobState::Cancelled);
    QCOMPARE(cancelledJob.errorCategory, DownloadErrorCategory::Cancelled);
    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::downloadCoordinator_cancelCurrentBeforeFirstStart_cancelsQueuedJobAndContinuesBatch()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    resolver.queueSuccess({QStringLiteral("https://fake.test/queued-next.ts")}, false);
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-after-queued-cancel"));
    decryptStage.queueSuccess(QStringLiteral("decrypt-after-queued-cancel"));

    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startBatch({
        makeCoordinatorJob(QStringLiteral("job-queued-cancel"), QStringLiteral("guid-queued-cancel"), QStringLiteral("Queued Cancel"), QStringLiteral("1080P"), QStringLiteral("C:/fake/queued-cancel")),
        makeCoordinatorJob(QStringLiteral("job-after-queued-cancel"), QStringLiteral("guid-after-queued-cancel"), QStringLiteral("After Queued Cancel"), QStringLiteral("1080P"), QStringLiteral("C:/fake/queued-cancel"))
    }));

    coordinator.cancelCurrent();

    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 1000);
    QCOMPARE(jobFinishedSpy.count(), 2);
    QCOMPARE(resolver.lastGuid(), QStringLiteral("guid-after-queued-cancel"));
    QCOMPARE(coordinator.cancelledJobs(), 1);
    QCOMPARE(coordinator.completedJobs(), 1);

    const auto firstJob = qvariant_cast<DownloadJob>(jobFinishedSpy.at(0).at(0));
    const auto secondJob = qvariant_cast<DownloadJob>(jobFinishedSpy.at(1).at(0));
    QCOMPARE(firstJob.id, QStringLiteral("job-queued-cancel"));
    QCOMPARE(firstJob.state, DownloadJobState::Cancelled);
    QCOMPARE(firstJob.errorCategory, DownloadErrorCategory::Cancelled);
    QCOMPARE(secondJob.id, QStringLiteral("job-after-queued-cancel"));
    QCOMPARE(secondJob.state, DownloadJobState::Completed);
}

void CoreRegressionTests::downloadCoordinator_cancelCurrentWhileConcatenating_cancelsJobAndDoesNotStartLaterStage()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    resolver.queueSuccess({QStringLiteral("https://fake.test/concat-cancel.ts")}, false);
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-should-cancel"), 50);
    decryptStage.queueSuccess(QStringLiteral("decrypt-should-not-start"));

    QSignalSpy jobChangedSpy(&coordinator, &DownloadCoordinator::jobChanged);
    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startSingle(makeCoordinatorJob(QStringLiteral("job-concat-cancel"),
        QStringLiteral("guid-concat-cancel"),
        QStringLiteral("节目拼接取消"),
        QStringLiteral("1080P"),
        QStringLiteral("C:/fake/concat-cancel"))));

    QTRY_VERIFY_WITH_TIMEOUT(jobChangedSpy.count() >= 4, 1000);
    coordinator.cancelCurrent();

    QVERIFY(batchFinishedSpy.wait(1000));
    QCOMPARE(jobFinishedSpy.count(), 1);

    const auto cancelledJob = qvariant_cast<DownloadJob>(jobFinishedSpy.takeFirst().at(0));
    QCOMPARE(cancelledJob.state, DownloadJobState::Cancelled);
    QCOMPARE(cancelledJob.errorCategory, DownloadErrorCategory::Cancelled);
    QCOMPARE(decryptStage.name(), QString());
    QCOMPARE(directFinalizeStage.title(), QString());
}

void CoreRegressionTests::downloadCoordinator_cancelCurrentWhileDecrypting_cancelsJob()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    resolver.queueSuccess({QStringLiteral("https://fake.test/decrypt-cancel.ts")}, false);
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-before-decrypt-cancel"));
    decryptStage.queueSuccess(QStringLiteral("decrypt-should-cancel"), 50);

    QSignalSpy jobChangedSpy(&coordinator, &DownloadCoordinator::jobChanged);
    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startSingle(makeCoordinatorJob(QStringLiteral("job-decrypt-cancel"),
        QStringLiteral("guid-decrypt-cancel"),
        QStringLiteral("节目解密取消"),
        QStringLiteral("1080P"),
        QStringLiteral("C:/fake/decrypt-cancel"))));

    QTRY_VERIFY_WITH_TIMEOUT(jobChangedSpy.count() >= 5, 1000);
    coordinator.cancelCurrent();

    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 1000);
    QCOMPARE(jobFinishedSpy.count(), 1);

    const auto cancelledJob = qvariant_cast<DownloadJob>(jobFinishedSpy.takeFirst().at(0));
    QCOMPARE(cancelledJob.state, DownloadJobState::Cancelled);
    QCOMPARE(cancelledJob.errorCategory, DownloadErrorCategory::Cancelled);
    QCOMPARE(directFinalizeStage.title(), QString());
}

void CoreRegressionTests::downloadCoordinator_cancelCurrentWhileDirectFinalizing_cancelsJob()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    resolver.queueSuccess({QStringLiteral("https://fake.test/direct-finalize-cancel.ts")}, true);
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-before-direct-finalize-cancel"));
    directFinalizeStage.queueSuccess(QStringLiteral("published_ts"), QStringLiteral("finalize-should-cancel"), QStringLiteral("C:/fake/direct-finalize-cancel.ts"), 50);

    QSignalSpy jobChangedSpy(&coordinator, &DownloadCoordinator::jobChanged);
    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startSingle(makeCoordinatorJob(QStringLiteral("job-direct-finalize-cancel"),
        QStringLiteral("guid-direct-finalize-cancel"),
        QStringLiteral("节目收尾取消"),
        QStringLiteral("4K"),
        QStringLiteral("C:/fake/direct-finalize-cancel"))));

    QTRY_VERIFY_WITH_TIMEOUT(jobChangedSpy.count() >= 5, 1000);
    coordinator.cancelCurrent();

    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 1000);
    QCOMPARE(jobFinishedSpy.count(), 1);

    const auto cancelledJob = qvariant_cast<DownloadJob>(jobFinishedSpy.takeFirst().at(0));
    QCOMPARE(cancelledJob.state, DownloadJobState::Cancelled);
    QCOMPARE(cancelledJob.errorCategory, DownloadErrorCategory::Cancelled);
    QCOMPARE(decryptStage.name(), QString());
}

void CoreRegressionTests::downloadCoordinator_cancelAllWhileConcatenating_cancelsQueuedJobsAndEmitsBatchFinishedOnce()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    resolver.queueSuccess({QStringLiteral("https://fake.test/cancel-all-active.ts")}, false);
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-cancel-all"), 50);

    QSignalSpy jobChangedSpy(&coordinator, &DownloadCoordinator::jobChanged);
    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);
    connect(&coordinator, &DownloadCoordinator::batchFinished, &coordinator, [&coordinator]() {
        coordinator.cancelAll();
    });

    QVERIFY(coordinator.startBatch({
        makeCoordinatorJob(QStringLiteral("job-cancel-all-active"), QStringLiteral("guid-cancel-all-active"), QStringLiteral("Cancel All Active"), QStringLiteral("1080P"), QStringLiteral("C:/fake/cancel-all-active")),
        makeCoordinatorJob(QStringLiteral("job-cancel-all-queued"), QStringLiteral("guid-cancel-all-queued"), QStringLiteral("Cancel All Queued"), QStringLiteral("1080P"), QStringLiteral("C:/fake/cancel-all-active"))
    }));

    QTRY_VERIFY_WITH_TIMEOUT(jobChangedSpy.count() >= 4, 1000);
    coordinator.cancelAll();

    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 1000);
    QCOMPARE(jobFinishedSpy.count(), 2);
    QCOMPARE(resolver.lastGuid(), QStringLiteral("guid-cancel-all-active"));
    QCOMPARE(decryptStage.name(), QString());
    QCOMPARE(directFinalizeStage.title(), QString());

    int cancelledCount = 0;
    QStringList finishedIds;
    for (int index = 0; index < jobFinishedSpy.count(); ++index) {
        const auto job = qvariant_cast<DownloadJob>(jobFinishedSpy.at(index).at(0));
        finishedIds.append(job.id);
        if (job.state == DownloadJobState::Cancelled && job.errorCategory == DownloadErrorCategory::Cancelled) {
            ++cancelledCount;
        }
    }

    QCOMPARE(cancelledCount, 2);
    QVERIFY(finishedIds.contains(QStringLiteral("job-cancel-all-active")));
    QVERIFY(finishedIds.contains(QStringLiteral("job-cancel-all-queued")));

    const auto batchArgs = batchFinishedSpy.takeFirst();
    QCOMPARE(batchArgs.at(0).toInt(), 0);
    QCOMPARE(batchArgs.at(1).toInt(), 0);
    QCOMPARE(batchArgs.at(2).toInt(), 2);
    QCOMPARE(batchArgs.at(3).toInt(), 2);
    QCOMPARE(batchArgs.at(4).toBool(), false);
}

void CoreRegressionTests::downloadCoordinator_cancelAllRepeatedly_emitsBatchFinishedExactlyOnce()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    resolver.queueSuccess({QStringLiteral("https://fake.test/repeat-cancel.ts")}, false);
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-repeat-cancel"), 200);

    QSignalSpy jobChangedSpy(&coordinator, &DownloadCoordinator::jobChanged);
    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startBatch({
        makeCoordinatorJob(QStringLiteral("job-repeat-cancel-active"), QStringLiteral("guid-repeat-cancel-active"), QStringLiteral("Repeat Cancel Active"), QStringLiteral("1080P"), QStringLiteral("C:/fake/repeat-cancel")),
        makeCoordinatorJob(QStringLiteral("job-repeat-cancel-queued"), QStringLiteral("guid-repeat-cancel-queued"), QStringLiteral("Repeat Cancel Queued"), QStringLiteral("1080P"), QStringLiteral("C:/fake/repeat-cancel"))
    }));

    QTRY_VERIFY_WITH_TIMEOUT(jobChangedSpy.count() >= 4, 1000);

    coordinator.cancelAll();
    coordinator.cancelAll();
    coordinator.cancelAll();

    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 1000);
    QCOMPARE(jobFinishedSpy.count(), 2);
    QCOMPARE(coordinator.cancelledJobs(), 2);

    coordinator.cancelAll();
    QTest::qWait(20);
    QCOMPARE(batchFinishedSpy.count(), 1);
}

void CoreRegressionTests::downloadCoordinator_decryptSharedEnvironmentFailure_stopsBatch_onCboxMissing()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    resolver.queueSuccess({QStringLiteral("https://fake.test/cbox-stop.ts")}, false);
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-cbox-stop"));
    decryptStage.queueFailure(QStringLiteral("解密失败 [code=cbox_missing]: decrypt/cbox.exe 不存在"));

    QSignalSpy fatalSpy(&coordinator, &DownloadCoordinator::fatalBatchFailure);
    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startBatch({
        makeCoordinatorJob(QStringLiteral("job-cbox-stop"), QStringLiteral("guid-cbox-stop"), QStringLiteral("节目缺少CBox"), QStringLiteral("1080P"), QStringLiteral("C:/fake/cbox-stop")),
        makeCoordinatorJob(QStringLiteral("job-cbox-stop-next"), QStringLiteral("guid-cbox-stop-next"), QStringLiteral("节目后续应取消"), QStringLiteral("1080P"), QStringLiteral("C:/fake/cbox-stop"))
    }));

    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 1000);
    QCOMPARE(fatalSpy.count(), 1);
    QCOMPARE(jobFinishedSpy.count(), 2);

    const auto failedJob = qvariant_cast<DownloadJob>(jobFinishedSpy.at(0).at(0));
    const auto cancelledJob = qvariant_cast<DownloadJob>(jobFinishedSpy.at(1).at(0));
    QCOMPARE(failedJob.state, DownloadJobState::Failed);
    QCOMPARE(failedJob.errorCategory, DownloadErrorCategory::FileSystemError);
    QCOMPARE(cancelledJob.state, DownloadJobState::Cancelled);
}

void CoreRegressionTests::downloadCoordinator_directFinalizeSharedEnvironmentFailure_stopsBatch_onOutputUnwritable()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    resolver.queueSuccess({QStringLiteral("https://fake.test/output-stop.ts")}, true);
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-output-stop"));
    directFinalizeStage.queueFailure(QStringLiteral("output_unwritable"), QStringLiteral("synthetic output unwritable"));

    QSignalSpy fatalSpy(&coordinator, &DownloadCoordinator::fatalBatchFailure);
    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startBatch({
        makeCoordinatorJob(QStringLiteral("job-output-stop"), QStringLiteral("guid-output-stop"), QStringLiteral("节目输出不可写"), QStringLiteral("4K"), QStringLiteral("C:/fake/output-stop")),
        makeCoordinatorJob(QStringLiteral("job-output-stop-next"), QStringLiteral("guid-output-stop-next"), QStringLiteral("节目后续应取消"), QStringLiteral("1080P"), QStringLiteral("C:/fake/output-stop"))
    }));

    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 1000);
    QCOMPARE(fatalSpy.count(), 1);
    QCOMPARE(jobFinishedSpy.count(), 2);

    const auto failedJob = qvariant_cast<DownloadJob>(jobFinishedSpy.at(0).at(0));
    const auto cancelledJob = qvariant_cast<DownloadJob>(jobFinishedSpy.at(1).at(0));
    QCOMPARE(failedJob.state, DownloadJobState::Failed);
    QCOMPARE(failedJob.errorCategory, DownloadErrorCategory::FileSystemError);
    QCOMPARE(cancelledJob.state, DownloadJobState::Cancelled);
}

void CoreRegressionTests::downloadCoordinator_ownedDownloadStage_recoveryFailureThenSuccess_completesJob()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&apiService, &concatStage, &decryptStage, &directFinalizeStage);

    const QString guid = QStringLiteral("coordinator-owned-download-recovery-guid");
    QUrl infoUrl(QStringLiteral("https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do"));
    QUrlQuery infoQuery;
    infoQuery.addQueryItem(QStringLiteral("pid"), guid);
    infoUrl.setQuery(infoQuery);
    manager.queueSuccess(infoUrl, QByteArray(R"({"manifest":{"hls_enc2_url":"https://media.example/asp/enc2/master.m3u8"}})"));
    manager.queueSuccess(QUrl(QStringLiteral("https://drm.cntv.vod.dnsv1.com/asp/enc2/master.m3u8")),
        QByteArray("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1228800\n/video/720/index.m3u8\n"));
    manager.queueSuccess(QUrl(QStringLiteral("https://drm.cntv.vod.dnsv1.com/video/720/index.m3u8")),
        QByteArray("#EXTM3U\n#EXTINF:2.0,\n0001.ts\n"));

    const QUrl shardUrl(QStringLiteral("https://drm.cntv.vod.dnsv1.com/video/720/0001.ts"));
    manager.queueError(shardUrl, QNetworkReply::ConnectionRefusedError, QStringLiteral("initial recovery trigger error 1"));
    manager.queueError(shardUrl, QNetworkReply::ConnectionRefusedError, QStringLiteral("initial recovery trigger error 2"));
    manager.queueSuccess(shardUrl, QByteArray("coordinator-recovered-segment"));

    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);
    DownloadCoordinatorTestAdapter::setTestDownloadReplyFactory(coordinator, [&manager](const QNetworkRequest& request) {
        return manager.createReplyForRequest(QNetworkAccessManager::GetOperation, request);
    });
    DownloadCoordinatorTestAdapter::setTestDownloadPolicies(coordinator,
        200,
        2,
        5,
        400,
        2,
        5);

    concatStage.queueSuccess(QStringLiteral("concat-after-recovery"));
    decryptStage.queueSuccess(QStringLiteral("decrypt-after-recovery"));

    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath(QStringLiteral("coordinator-owned-recovery"));
    QVERIFY(coordinator.startSingle(makeCoordinatorJob(QStringLiteral("job-owned-recovery"), guid, QStringLiteral("节目补拉成功"), QStringLiteral("2"), savePath)));

    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 4000);
    QCOMPARE(jobFinishedSpy.count(), 1);
    QCOMPARE(concatStage.startCount(), 1);
    QCOMPARE(manager.requestCount(), 6);
    QCOMPARE(manager.unexpectedRequestCount(), 0);

    const auto finishedJob = qvariant_cast<DownloadJob>(jobFinishedSpy.takeFirst().at(0));
    QCOMPARE(finishedJob.id, QStringLiteral("job-owned-recovery"));
    QCOMPARE(finishedJob.state, DownloadJobState::Completed);
    QCOMPARE(finishedJob.errorCategory, DownloadErrorCategory::Unknown);

    const QString taskDir = QDir(savePath).filePath(coordinatorTaskHash(QStringLiteral("节目补拉成功"), QStringLiteral("job-owned-recovery")));
    QFile recoveredFile(QDir(taskDir).filePath(QStringLiteral("0001.ts")));
    QVERIFY(recoveredFile.open(QIODevice::ReadOnly));
    QCOMPARE(recoveredFile.readAll(), QByteArray("coordinator-recovered-segment"));
    QVERIFY(!QFileInfo::exists(QDir(taskDir).filePath(QStringLiteral(".download_state.json"))));

    DownloadCoordinatorTestAdapter::clearTestDownloadReplyFactory(coordinator);
}

void CoreRegressionTests::downloadCoordinator_ownedDownloadStage_cancelDuringDownload_abortsReplyAndPreventsConcat()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&apiService, &concatStage, &decryptStage, &directFinalizeStage);

    const QString guid = QStringLiteral("coordinator-owned-download-cancel-guid");
    QUrl infoUrl(QStringLiteral("https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do"));
    QUrlQuery infoQuery;
    infoQuery.addQueryItem(QStringLiteral("pid"), guid);
    infoUrl.setQuery(infoQuery);
    manager.queueSuccess(infoUrl, QByteArray(R"({"manifest":{"hls_enc2_url":"https://media.example/asp/enc2/master.m3u8"}})"));
    manager.queueSuccess(QUrl(QStringLiteral("https://drm.cntv.vod.dnsv1.com/asp/enc2/master.m3u8")),
        QByteArray("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1228800\n/video/720/index.m3u8\n"));
    manager.queueSuccess(QUrl(QStringLiteral("https://drm.cntv.vod.dnsv1.com/video/720/index.m3u8")),
        QByteArray("#EXTM3U\n#EXTINF:2.0,\n0001.ts\n"));

    const QUrl shardUrl(QStringLiteral("https://drm.cntv.vod.dnsv1.com/video/720/0001.ts"));
    manager.queueSuccess(shardUrl, QByteArray("slow-segment"), 200);

    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);
    DownloadCoordinatorTestAdapter::setTestDownloadReplyFactory(coordinator, [&manager](const QNetworkRequest& request) {
        return manager.createReplyForRequest(QNetworkAccessManager::GetOperation, request);
    });
    DownloadCoordinatorTestAdapter::setTestDownloadPolicies(coordinator,
        500,
        2,
        5,
        500,
        2,
        5);

    concatStage.queueSuccess(QStringLiteral("should-not-run"));

    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath(QStringLiteral("coordinator-owned-cancel"));
    QVERIFY(coordinator.startSingle(makeCoordinatorJob(QStringLiteral("job-owned-cancel"), guid, QStringLiteral("节目下载取消"), QStringLiteral("2"), savePath)));

    QTRY_VERIFY_WITH_TIMEOUT(manager.requestCount() >= 4, 2000);
    FakeNetworkReply* pendingReply = manager.lastReply();
    QVERIFY(pendingReply != nullptr);
    QCOMPARE(manager.requestedUrls().constLast(), shardUrl);

    coordinator.cancelCurrent();

    QVERIFY(batchFinishedSpy.wait(3000));
    QCOMPARE(jobFinishedSpy.count(), 1);
    QVERIFY(pendingReply->wasAborted());
    QCOMPARE(concatStage.startCount(), 0);

    const auto finishedJob = qvariant_cast<DownloadJob>(jobFinishedSpy.takeFirst().at(0));
    QCOMPARE(finishedJob.id, QStringLiteral("job-owned-cancel"));
    QCOMPARE(finishedJob.state, DownloadJobState::Cancelled);
    QCOMPARE(finishedJob.errorCategory, DownloadErrorCategory::Cancelled);

    DownloadCoordinatorTestAdapter::clearTestDownloadReplyFactory(coordinator);
}

void CoreRegressionTests::downloadCoordinator_ownedDownloadStage_duplicateSameTitleJobs_useDistinctTaskDirectories()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&apiService, &concatStage, &decryptStage, &directFinalizeStage);

    const QString guid = QStringLiteral("coordinator-owned-duplicate-guid");
    QUrl infoUrl(QStringLiteral("https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do"));
    QUrlQuery infoQuery;
    infoQuery.addQueryItem(QStringLiteral("pid"), guid);
    infoUrl.setQuery(infoQuery);

    const QUrl masterUrl(QStringLiteral("https://drm.cntv.vod.dnsv1.com/asp/enc2/master.m3u8"));
    const QUrl playlistUrl(QStringLiteral("https://drm.cntv.vod.dnsv1.com/video/720/index.m3u8"));
    const QUrl shardUrl(QStringLiteral("https://drm.cntv.vod.dnsv1.com/video/720/0001.ts"));
    const QByteArray shardBody("duplicate-workspace-segment");
    for (int i = 0; i < 2; ++i) {
        manager.queueSuccess(infoUrl, QByteArray(R"({"manifest":{"hls_enc2_url":"https://media.example/asp/enc2/master.m3u8"}})"));
        manager.queueSuccess(masterUrl,
            QByteArray("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1228800\n/video/720/index.m3u8\n"));
        manager.queueSuccess(playlistUrl,
            QByteArray("#EXTM3U\n#EXTINF:2.0,\n0001.ts\n"));
        manager.queueSuccess(shardUrl, shardBody);
    }

    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);
    DownloadCoordinatorTestAdapter::setTestDownloadReplyFactory(coordinator, [&manager](const QNetworkRequest& request) {
        return manager.createReplyForRequest(QNetworkAccessManager::GetOperation, request);
    });
    DownloadCoordinatorTestAdapter::setTestDownloadPolicies(coordinator,
        200,
        2,
        5,
        400,
        2,
        5);

    concatStage.queueSuccess(QStringLiteral("concat-duplicate-a"));
    concatStage.queueSuccess(QStringLiteral("concat-duplicate-b"));
    decryptStage.queueSuccess(QStringLiteral("decrypt-duplicate-a"));
    decryptStage.queueSuccess(QStringLiteral("decrypt-duplicate-b"));

    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath(QStringLiteral("coordinator-owned-duplicate"));
    const QString title = QStringLiteral("重复节目");
    const DownloadJob firstJob = makeCoordinatorJob(QStringLiteral("job-duplicate-a"), guid, title, QStringLiteral("2"), savePath);
    const DownloadJob secondJob = makeCoordinatorJob(QStringLiteral("job-duplicate-b"), guid, title, QStringLiteral("2"), savePath);
    QVERIFY(coordinator.startBatch({ firstJob, secondJob }));

    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 4000);
    QCOMPARE(jobFinishedSpy.count(), 2);
    QCOMPARE(concatStage.startCount(), 2);
    QCOMPARE(manager.requestCount(), 8);
    QCOMPARE(manager.unexpectedRequestCount(), 0);

    const QString firstTaskDir = QDir(savePath).filePath(coordinatorTaskHash(title, QStringLiteral("job-duplicate-a")));
    const QString secondTaskDir = QDir(savePath).filePath(coordinatorTaskHash(title, QStringLiteral("job-duplicate-b")));
    QVERIFY(firstTaskDir != secondTaskDir);
    QVERIFY(QFileInfo::exists(QDir(firstTaskDir).filePath(QStringLiteral("0001.ts"))));
    QVERIFY(QFileInfo::exists(QDir(secondTaskDir).filePath(QStringLiteral("0001.ts"))));

    QFile firstShard(QDir(firstTaskDir).filePath(QStringLiteral("0001.ts")));
    QFile secondShard(QDir(secondTaskDir).filePath(QStringLiteral("0001.ts")));
    QVERIFY(firstShard.open(QIODevice::ReadOnly));
    QVERIFY(secondShard.open(QIODevice::ReadOnly));
    QCOMPARE(firstShard.readAll(), shardBody);
    QCOMPARE(secondShard.readAll(), shardBody);
    QVERIFY(!QFileInfo::exists(QDir(firstTaskDir).filePath(QStringLiteral(".download_state.json"))));
    QVERIFY(!QFileInfo::exists(QDir(secondTaskDir).filePath(QStringLiteral(".download_state.json"))));

    DownloadCoordinatorTestAdapter::clearTestDownloadReplyFactory(coordinator);
}

void CoreRegressionTests::downloadCoordinator_ownedDecryptStage_teardownWhileActive_defersWorkerDeletionUntilThreadFinishes()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    auto coordinator = std::make_unique<DownloadCoordinator>(&resolver, &downloadStage, &concatStage, nullptr, &directFinalizeStage);

    const QString title = QStringLiteral("节目解密销毁中");
    const QString savePath = QDir(m_tempDir->path()).filePath(QStringLiteral("coordinator-owned-decrypt-teardown"));
    const QString taskDir = QDir(savePath).filePath(coordinatorTaskHash(title, QStringLiteral("job-owned-decrypt-teardown")));
    QVERIFY(QDir().mkpath(taskDir));
    QVERIFY(createFakeTsFile(QDir(taskDir).filePath(QStringLiteral("result.ts")), 4, 777));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());
    DownloadCoordinatorTestAdapter::setTestDecryptAssetsDir(*coordinator, decryptAssetsDir.path());
    DownloadCoordinatorTestAdapter::setTestDecryptStageShutdownWaitMs(*coordinator, 0);

    auto runnerStarted = std::make_shared<std::atomic_bool>(false);
    auto cancelSeen = std::make_shared<std::atomic_bool>(false);
    auto allowExit = std::make_shared<std::atomic_bool>(false);
    QMutex lifecycleMutex;
    QStringList lifecycleEvents;

    DownloadCoordinatorTestAdapter::setTestDecryptStageLifecycleObserver(*coordinator,
        [&lifecycleMutex, &lifecycleEvents](const QString& event) {
            QMutexLocker locker(&lifecycleMutex);
            lifecycleEvents.append(event);
        });
    DownloadCoordinatorTestAdapter::setTestDecryptProcessRunner(*coordinator,
        [runnerStarted, cancelSeen, allowExit](const DecryptProcessRequest& request) -> DecryptProcessResult {
            runnerStarted->store(true, std::memory_order_relaxed);
            while (!allowExit->load(std::memory_order_relaxed)) {
                if (request.cancellationRequested && request.cancellationRequested()) {
                    cancelSeen->store(true, std::memory_order_relaxed);
                }
                QThread::msleep(5);
            }

            DecryptProcessResult result;
            result.started = true;
            result.cancelled = cancelSeen->load(std::memory_order_relaxed)
                || (request.cancellationRequested && request.cancellationRequested());
            return result;
        });

    resolver.queueSuccess({QStringLiteral("https://fake.test/owned-decrypt-teardown-1.ts")}, false);
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-before-owned-decrypt-teardown"));

    QVERIFY(coordinator->startSingle(makeCoordinatorJob(QStringLiteral("job-owned-decrypt-teardown"),
        QStringLiteral("guid-owned-decrypt-teardown"),
        title,
        QStringLiteral("1080P"),
        savePath)));

    QTRY_VERIFY_WITH_TIMEOUT(runnerStarted->load(std::memory_order_relaxed), 2000);

    auto hasLifecycleEvent = [&lifecycleMutex, &lifecycleEvents](const QString& event) {
        QMutexLocker locker(&lifecycleMutex);
        return lifecycleEvents.contains(event);
    };
    auto lifecycleEventIndex = [&lifecycleMutex, &lifecycleEvents](const QString& event) {
        QMutexLocker locker(&lifecycleMutex);
        return lifecycleEvents.indexOf(event);
    };

    coordinator.reset();
    QTest::qWait(50);
    QVERIFY(!hasLifecycleEvent(QStringLiteral("worker_destroyed")));

    allowExit->store(true, std::memory_order_relaxed);
    QTRY_VERIFY_WITH_TIMEOUT(cancelSeen->load(std::memory_order_relaxed), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(hasLifecycleEvent(QStringLiteral("thread_finished")), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(hasLifecycleEvent(QStringLiteral("worker_destroyed")), 2000);
    QVERIFY(lifecycleEventIndex(QStringLiteral("thread_finished")) < lifecycleEventIndex(QStringLiteral("worker_destroyed")));
}

void CoreRegressionTests::downloadCoordinator_ownedConcatStage_mergesTaskDirectoryAndStartsDecrypt()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, nullptr, &decryptStage, &directFinalizeStage);

    const QString title = QStringLiteral("节目真实拼接");
    const QString savePath = QDir(m_tempDir->path()).filePath(QStringLiteral("coordinator-owned-concat"));
    const QString taskDir = QDir(savePath).filePath(coordinatorTaskHash(title, QStringLiteral("job-owned-concat")));
    QVERIFY(QDir().mkpath(taskDir));
    QVERIFY(createFakeTsFile(QDir(taskDir).filePath(QStringLiteral("0001.ts")), 2, 0));
    QVERIFY(createFakeTsFile(QDir(taskDir).filePath(QStringLiteral("0002.ts")), 2, 256));

    resolver.queueSuccess({QStringLiteral("https://fake.test/owned-concat-1.ts")}, false);
    downloadStage.queueSuccess({{100, 100}});
    decryptStage.queueSuccess(QStringLiteral("decrypt-after-owned-concat"));

    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startSingle(makeCoordinatorJob(QStringLiteral("job-owned-concat"),
        QStringLiteral("guid-owned-concat"),
        title,
        QStringLiteral("1080P"),
        savePath)));

    QVERIFY(batchFinishedSpy.wait(3000));
    QCOMPARE(jobFinishedSpy.count(), 1);
    QCOMPARE(decryptStage.name(), title);
    QCOMPARE(decryptStage.savePath(), savePath);
    QCOMPARE(decryptStage.transcodeToMp4(), false);
    QVERIFY(QFileInfo::exists(QDir(taskDir).filePath(QStringLiteral("result.ts"))));

    const auto finishedJob = qvariant_cast<DownloadJob>(jobFinishedSpy.takeFirst().at(0));
    QCOMPARE(finishedJob.state, DownloadJobState::Completed);
}

void CoreRegressionTests::downloadCoordinator_ownedDecryptStage_completesJob()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, nullptr, &directFinalizeStage);

    const QString title = QStringLiteral("节目真实解密");
    const QString savePath = QDir(m_tempDir->path()).filePath(QStringLiteral("coordinator-owned-decrypt"));
    const QString taskDir = QDir(savePath).filePath(coordinatorTaskHash(title, QStringLiteral("job-owned-decrypt")));
    QVERIFY(QDir().mkpath(taskDir));
    QVERIFY(createFakeTsFile(QDir(taskDir).filePath(QStringLiteral("result.ts")), 4, 256));

    QTemporaryDir decryptAssetsDir;
    QVERIFY(decryptAssetsDir.isValid());
    createDecryptAssets(decryptAssetsDir.path());
    DownloadCoordinatorTestAdapter::setTestDecryptAssetsDir(coordinator, decryptAssetsDir.path());
    DownloadCoordinatorTestAdapter::setTestDecryptProcessRunner(coordinator,
        [](const DecryptProcessRequest& request) -> DecryptProcessResult {
            createFakeTsFile(request.arguments.at(1), 4, 512);
            DecryptProcessResult result;
            result.started = true;
            result.exitCode = 0;
            result.exitStatus = QProcess::NormalExit;
            return result;
        });

    resolver.queueSuccess({QStringLiteral("https://fake.test/owned-decrypt-1.ts")}, false);
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-before-owned-decrypt"));

    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startSingle(makeCoordinatorJob(QStringLiteral("job-owned-decrypt"),
        QStringLiteral("guid-owned-decrypt"),
        title,
        QStringLiteral("1080P"),
        savePath)));

    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 4000);
    QCOMPARE(jobFinishedSpy.count(), 1);
    QVERIFY(QFileInfo::exists(QDir(savePath).filePath(title + QStringLiteral(".ts"))));
    QVERIFY(!QFileInfo::exists(taskDir));

    const auto finishedJob = qvariant_cast<DownloadJob>(jobFinishedSpy.takeFirst().at(0));
    QCOMPARE(finishedJob.state, DownloadJobState::Completed);

    DownloadCoordinatorTestAdapter::clearTestDecryptProcessRunner(coordinator);
    DownloadCoordinatorTestAdapter::clearTestDecryptAssetsDir(coordinator);
}

void CoreRegressionTests::downloadCoordinator_ownedDirectFinalizeStage_completes4kJob()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, nullptr);

    const QString title = QStringLiteral("节目真实4K收尾");
    const QString savePath = QDir(m_tempDir->path()).filePath(QStringLiteral("coordinator-owned-direct-finalize"));
    const QString taskDir = QDir(savePath).filePath(coordinatorTaskHash(title, QStringLiteral("job-owned-direct-finalize")));
    QVERIFY(QDir().mkpath(taskDir));
    QVERIFY(createFakeTsFile(QDir(taskDir).filePath(QStringLiteral("result.ts")), 4, 601));

    resolver.queueSuccess({QStringLiteral("https://fake.test/owned-direct-1.ts")}, true);
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-before-owned-direct-finalize"));

    QSignalSpy jobFinishedSpy(&coordinator, &DownloadCoordinator::jobFinished);
    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startSingle(makeCoordinatorJob(QStringLiteral("job-owned-direct-finalize"),
        QStringLiteral("guid-owned-direct-finalize"),
        title,
        QStringLiteral("4K"),
        savePath)));

    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 4000);
    QCOMPARE(jobFinishedSpy.count(), 1);
    QCOMPARE(decryptStage.name(), QString());
    QVERIFY(QFileInfo::exists(QDir(savePath).filePath(title + QStringLiteral(".ts"))));
    QVERIFY(!QFileInfo::exists(taskDir));

    const auto finishedJob = qvariant_cast<DownloadJob>(jobFinishedSpy.takeFirst().at(0));
    QCOMPARE(finishedJob.state, DownloadJobState::Completed);
}

void CoreRegressionTests::cctvVideoDownloader_openDownloadDialog_withoutSelection_showsWarningAndDoesNotStartBatch()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    QSignalSpy busyChangedSpy(&coordinator, &DownloadCoordinator::busyChanged);
    QSignalSpy batchStartedSpy(&coordinator, &DownloadCoordinator::batchStarted);

    QVERIFY(!coordinator.startBatch({}));
    QVERIFY(!coordinator.isBusy());
    QCOMPARE(busyChangedSpy.count(), 0);
    QCOMPARE(batchStartedSpy.count(), 0);

    const QString sourcePath = QDir(QStringLiteral(PROJECT_SOURCE_DIR))
        .filePath(QStringLiteral("src/source/cctvvideodownloader.cpp"));
    QFile sourceFile(sourcePath);
    QVERIFY2(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(sourcePath));

    const QString source = QString::fromUtf8(sourceFile.readAll());
    const int emptySelectionBranch = source.indexOf(QStringLiteral("if (selectedIndexes.empty())"));
    const int warningCall = source.indexOf(QStringLiteral("QMessageBox::warning(this, \"Warning\", \"请先选择要下载的视频！\");"));
    const int openProgressWindow = source.indexOf(QStringLiteral("m_downloadProgressWindow->open();"));
    const int startCoordinator = source.indexOf(QStringLiteral("m_downloadCoordinator->startSingle("));

    QVERIFY(emptySelectionBranch >= 0);
    QVERIFY(warningCall > emptySelectionBranch);
    QVERIFY(openProgressWindow > warningCall);
    QVERIFY(startCoordinator > warningCall);
}

void CoreRegressionTests::downloadProgressWindow_show_opensNonBlockingWindow()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    DownloadProgressWindow window(&coordinator);
    window.show();
    QTest::qWait(50);

    QVERIFY(window.isVisible());
    QVERIFY(!window.isHidden());
}

void CoreRegressionTests::downloadProgressWindow_usesChineseStringsAndLegacyShardTable()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    DownloadProgressWindow window(&coordinator);
    window.show();
    QTest::qWait(50);

    QCOMPARE(window.windowTitle(), QStringLiteral("下载进度"));

    const QList<QProgressBar*> progressBars = window.findChildren<QProgressBar*>();
    QCOMPARE(progressBars.size(), 2);

    auto* tableView = window.findChild<QTableView*>();
    QVERIFY(tableView != nullptr);
    auto* model = tableView->model();
    QVERIFY(model != nullptr);
    QCOMPARE(model->columnCount(), 4);
    QCOMPARE(model->headerData(0, Qt::Horizontal).toString(), QStringLiteral("序号"));
    QCOMPARE(model->headerData(1, Qt::Horizontal).toString(), QStringLiteral("状态"));
    QCOMPARE(model->headerData(2, Qt::Horizontal).toString(), QStringLiteral("URL"));
    QCOMPARE(model->headerData(3, Qt::Horizontal).toString(), QStringLiteral("进度"));

    QStringList buttonTexts;
    for (const auto* button : window.findChildren<QPushButton*>()) {
        buttonTexts.append(button->text());
    }
    QVERIFY(buttonTexts.contains(QStringLiteral("取消当前")));
    QVERIFY(buttonTexts.contains(QStringLiteral("全部取消")));
}

void CoreRegressionTests::downloadProgressWindow_shardUpdates_populateLegacyModelRows()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    DownloadProgressWindow window(&coordinator);
    window.show();
    QTest::qWait(50);

    resolver.queueSuccess({
        QStringLiteral("https://fake.test/detail-1.ts"),
        QStringLiteral("https://fake.test/detail-2.ts")
    }, false);
    downloadStage.queueSuccess({{50, 100}, {100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-detail"));
    decryptStage.queueSuccess(QStringLiteral("decrypt-detail"));

    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startSingle(makeCoordinatorJob(QStringLiteral("job-detail-table"),
        QStringLiteral("guid-detail-table"),
        QStringLiteral("分片明细测试"),
        QStringLiteral("1080P"),
        QStringLiteral("C:/fake/detail-table"))));

    auto* tableView = window.findChild<QTableView*>();
    QVERIFY(tableView != nullptr);
    auto* model = tableView->model();
    QVERIFY(model != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount() == 2, 1000);

    QCOMPARE(model->data(model->index(0, 0)).toString(), QStringLiteral("1"));
    QCOMPARE(model->data(model->index(0, 1)).toString(), QStringLiteral("等待"));
    QCOMPARE(model->data(model->index(0, 2)).toString(), QStringLiteral("https://fake.test/detail-1.ts"));
    QCOMPARE(model->data(model->index(0, 3)).toString(), QStringLiteral("0%"));
    QCOMPARE(model->data(model->index(1, 0)).toString(), QStringLiteral("2"));
    QCOMPARE(model->data(model->index(1, 2)).toString(), QStringLiteral("https://fake.test/detail-2.ts"));

    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 1000);

    bool foundFinishedText = false;
    for (const auto* label : window.findChildren<QLabel*>()) {
        if (label->text() == QStringLiteral("批次完成")) {
            foundFinishedText = true;
            break;
        }
    }
    QVERIFY(foundFinishedText);
}

void CoreRegressionTests::downloadProgressWindow_coordinatorSignals_updateDisplay()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    DownloadProgressWindow window(&coordinator);
    window.show();
    QTest::qWait(50);

    resolver.queueSuccess({QStringLiteral("https://fake.test/p-1.ts")}, false);
    downloadStage.queueSuccess({{50, 100}, {100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-test"));
    decryptStage.queueSuccess(QStringLiteral("decrypt-test"));

    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startSingle(makeCoordinatorJob(QStringLiteral("job-signals"),
        QStringLiteral("guid-signals"),
        QStringLiteral("Test Video Title"),
        QStringLiteral("1080P"),
        QStringLiteral("C:/fake/signals-test"))));

    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 1000);

    QVERIFY(window.isVisible());
}

void CoreRegressionTests::downloadProgressWindow_cancelCurrent_callsCoordinatorCancel()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    DownloadProgressWindow window(&coordinator);
    window.show();
    QTest::qWait(50);

    resolver.queueSuccess({QStringLiteral("https://fake.test/cc-1.ts")}, false);

    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);
    QSignalSpy jobChangedSpy(&coordinator, &DownloadCoordinator::jobChanged);

    QVERIFY(coordinator.startSingle(makeCoordinatorJob(QStringLiteral("job-cancel-current"),
        QStringLiteral("guid-cancel-current"),
        QStringLiteral("Cancel Current Test"),
        QStringLiteral("1080P"),
        QStringLiteral("C:/fake/cancel-current"))));

    QTRY_VERIFY_WITH_TIMEOUT(jobChangedSpy.count() >= 2, 500);

    coordinator.cancelCurrent();

    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 500);
}

void CoreRegressionTests::downloadProgressWindow_cancelAll_callsCoordinatorCancelAll()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    DownloadProgressWindow window(&coordinator);
    window.show();
    QTest::qWait(50);

    resolver.queueSuccess({QStringLiteral("https://fake.test/ca-1.ts")}, false);
    resolver.queueSuccess({QStringLiteral("https://fake.test/ca-2.ts")}, false);
    downloadStage.queueSuccess({{100, 100}});
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-ca-1"), 5000);
    concatStage.queueSuccess(QStringLiteral("concat-ca-2"), 5000);
    decryptStage.queueSuccess(QStringLiteral("decrypt-ca-1"));
    decryptStage.queueSuccess(QStringLiteral("decrypt-ca-2"));

    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);
    QSignalSpy jobChangedSpy(&coordinator, &DownloadCoordinator::jobChanged);

    const QList<DownloadJob> jobs = {
        makeCoordinatorJob(QStringLiteral("job-ca-1"), QStringLiteral("guid-ca-1"), QStringLiteral("Video A"), QStringLiteral("1080P"), QStringLiteral("C:/fake/ca")),
        makeCoordinatorJob(QStringLiteral("job-ca-2"), QStringLiteral("guid-ca-2"), QStringLiteral("Video B"), QStringLiteral("720P"), QStringLiteral("C:/fake/ca"))
    };

    QVERIFY(coordinator.startBatch(jobs));
    QTRY_VERIFY_WITH_TIMEOUT(jobChangedSpy.count() >= 3, 500);

    coordinator.cancelAll();

    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 500);
    QVERIFY(coordinator.cancelledJobs() >= 1);
}

void CoreRegressionTests::downloadProgressWindow_batchFinished_disablesButtons()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    DownloadProgressWindow window(&coordinator);
    window.show();
    QTest::qWait(50);

    resolver.queueSuccess({QStringLiteral("https://fake.test/bf-1.ts")}, false);
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-bf"));
    decryptStage.queueSuccess(QStringLiteral("decrypt-bf"));

    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startSingle(makeCoordinatorJob(QStringLiteral("job-bf"),
        QStringLiteral("guid-bf"),
        QStringLiteral("Batch Finish Test"),
        QStringLiteral("1080P"),
        QStringLiteral("C:/fake/bf"))));

    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 1000);

    QVERIFY(!coordinator.isBusy());
    QVERIFY(window.isVisible());
}

void CoreRegressionTests::downloadProgressWindow_batchSummary_displaysMixedOutcomes()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    DownloadProgressWindow window(&coordinator);
    window.show();
    QTest::qWait(50);

    resolver.queueSuccess({QStringLiteral("https://fake.test/mix-s-1.ts")}, false);
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-mix-ok"));
    decryptStage.queueSuccess(QStringLiteral("decrypt-mix-ok"));

    resolver.queueFailure(DownloadErrorCategory::NetworkError, QStringLiteral("network error"));

    resolver.queueSuccess({QStringLiteral("https://fake.test/mix-s-3.ts")}, false);
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-mix-slow"), 5000);
    decryptStage.queueSuccess(QStringLiteral("decrypt-mix-unused"));

    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    const QList<DownloadJob> jobs = {
        makeCoordinatorJob(QStringLiteral("job-mix-1"), QStringLiteral("guid-mix-1"), QStringLiteral("Video Success"), QStringLiteral("1080P"), QStringLiteral("C:/fake/mix")),
        makeCoordinatorJob(QStringLiteral("job-mix-2"), QStringLiteral("guid-mix-2"), QStringLiteral("Video Failed"), QStringLiteral("720P"), QStringLiteral("C:/fake/mix")),
        makeCoordinatorJob(QStringLiteral("job-mix-3"), QStringLiteral("guid-mix-3"), QStringLiteral("Video Cancelled"), QStringLiteral("1080P"), QStringLiteral("C:/fake/mix"))
    };

    QVERIFY(coordinator.startBatch(jobs));

    QTimer::singleShot(200, &coordinator, &DownloadCoordinator::cancelCurrent);

    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 2000);

    QCOMPARE(coordinator.completedJobs(), 1);
    QCOMPARE(coordinator.failedJobs(), 1);
    QCOMPARE(coordinator.cancelledJobs(), 1);

    const auto args = batchFinishedSpy.takeFirst();
    const int signalCompleted = args.at(0).toInt();
    const int signalFailed = args.at(1).toInt();
    const int signalCancelled = args.at(2).toInt();
    const int signalTotal = args.at(3).toInt();
    const bool signalStoppedByFatal = args.at(4).toBool();

    QCOMPARE(signalCompleted, 1);
    QCOMPARE(signalFailed, 1);
    QCOMPARE(signalCancelled, 1);
    QCOMPARE(signalTotal, 3);
    QCOMPARE(signalStoppedByFatal, false);

    auto* queueLabel = window.findChild<QLabel*>("", Qt::FindChildrenRecursively);
    QVERIFY(queueLabel != nullptr);
}

void CoreRegressionTests::downloadProgressWindow_closeWhileActive_followsConfirmationDecisionPath()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    DownloadProgressWindow window(&coordinator);
    window.show();
    QTest::qWait(50);

    resolver.queueSuccess({QStringLiteral("https://fake.test/close-active.ts")}, false);
    downloadStage.queueSuccess({{100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-close-active"), 5000);

    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);
    QSignalSpy jobChangedSpy(&coordinator, &DownloadCoordinator::jobChanged);

    QVERIFY(coordinator.startBatch({
        makeCoordinatorJob(QStringLiteral("job-close-active"), QStringLiteral("guid-close-active"), QStringLiteral("Close Active"), QStringLiteral("1080P"), QStringLiteral("C:/fake/close-active")),
        makeCoordinatorJob(QStringLiteral("job-close-queued"), QStringLiteral("guid-close-queued"), QStringLiteral("Close Queued"), QStringLiteral("1080P"), QStringLiteral("C:/fake/close-active"))
    }));

    QTRY_VERIFY_WITH_TIMEOUT(jobChangedSpy.count() >= 4, 1000);

    int closePromptCount = 0;
    window.setTestCloseConfirmationCallback([&closePromptCount]() {
        ++closePromptCount;
        return closePromptCount == 1 ? QMessageBox::No : QMessageBox::Yes;
    });

    window.close();

    QCOMPARE(closePromptCount, 1);
    QVERIFY(window.isVisible());
    QVERIFY(coordinator.isBusy());
    QCOMPARE(batchFinishedSpy.count(), 0);

    window.close();

    QCOMPARE(closePromptCount, 2);
    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 1000);
    QVERIFY(window.isHidden());
    QVERIFY(!coordinator.isBusy());
    window.clearTestCloseConfirmationCallback();
}

void CoreRegressionTests::downloadCoordinator_eventLoopRemainsResponsiveDuringFakeLongBatch()
{
    FakeCoordinatorResolveService resolver;
    FakeCoordinatorDownloadStage downloadStage;
    FakeCoordinatorConcatStage concatStage;
    FakeCoordinatorDecryptStage decryptStage;
    FakeCoordinatorDirectFinalizeStage directFinalizeStage;
    DownloadCoordinator coordinator(&resolver, &downloadStage, &concatStage, &decryptStage, &directFinalizeStage);

    resolver.queueSuccess({QStringLiteral("https://fake.test/resp-1.ts")}, false);
    downloadStage.queueSuccess({{50, 100}, {100, 100}});
    concatStage.queueSuccess(QStringLiteral("concat-responsive"), 10000);
    decryptStage.queueSuccess(QStringLiteral("decrypt-responsive"));

    QSignalSpy batchFinishedSpy(&coordinator, &DownloadCoordinator::batchFinished);

    QVERIFY(coordinator.startSingle(makeCoordinatorJob(QStringLiteral("job-responsive"),
        QStringLiteral("guid-responsive"),
        QStringLiteral("Responsive Event Loop Test"),
        QStringLiteral("1080P"),
        QStringLiteral("C:/fake/responsive"))));

    QTimer timer;
    timer.setSingleShot(true);
    QSignalSpy timerSpy(&timer, &QTimer::timeout);
    timer.start(50);

    QTRY_VERIFY_WITH_TIMEOUT(timerSpy.count() == 1, 500);

    QVERIFY(coordinator.isBusy());

    coordinator.cancelAll();
    QTRY_VERIFY_WITH_TIMEOUT(batchFinishedSpy.count() == 1, 500);
}

void CoreRegressionTests::cctvVideoDownloader_openDownloadDialog_usesCoordinatorOnly()
{
    const QString sourcePath = QDir(QStringLiteral(PROJECT_SOURCE_DIR))
        .filePath(QStringLiteral("src/source/cctvvideodownloader.cpp"));
    QFile sourceFile(sourcePath);
    QVERIFY2(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(sourcePath));

    const QString source = QString::fromUtf8(sourceFile.readAll());
    QVERIFY(!source.contains(QStringLiteral("APIService::instance().getEncryptM3U8Urls(")));
    QVERIFY(!source.contains(QStringLiteral("Download dialog(this)")));
    QVERIFY(!source.contains(QStringLiteral("dialog.transferDwonloadParams(")));
    QVERIFY(!source.contains(QStringLiteral("void CCTVVideoDownloader::concatVideo()")));
    QVERIFY(!source.contains(QStringLiteral("void CCTVVideoDownloader::decryptVideo()")));
    QVERIFY(source.contains(QStringLiteral("m_downloadCoordinator->startSingle(")));
    QVERIFY(source.contains(QStringLiteral("m_downloadCoordinator->startBatch(")));
    QVERIFY(source.contains(QStringLiteral("m_downloadProgressWindow->open();")));
}

// ── Import dialog tests ──────────────────────────────────────

void CoreRegressionTests::importDialog_successPath_persistsProgramme()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    const QUrl url(QStringLiteral("https://tv.cctv.com/cctv4k/test-persist.shtml"));
    const QByteArray html = R"(
<html><head><script>
var guid = 'import-persist-guid';
</script></head></html>
)";
    manager.queueSuccess(url, html);
    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);

    initializeSettingsSandbox();

    Import importDialog(nullptr);
    auto* lineEdit = importDialog.findChild<QLineEdit*>("lineEdit");
    QVERIFY(lineEdit != nullptr);
    lineEdit->setText(url.toString());

    QSignalSpy resolvedSpy(&apiService, &APIService::playColumnInfoResolved);
    importDialog.ImportProgrammeFromUrl();

    QVERIFY(resolvedSpy.wait(1000));
    QCOMPARE(resolvedSpy.count(), 1);

    const QList<QVariant> resultArgs = resolvedSpy.takeFirst();
    QCOMPARE(resultArgs.at(1).toStringList(), QStringList({
        QStringLiteral("CCTV-4K"),
        QStringLiteral("import-persist-guid"),
        QStringLiteral("import-persist-guid")
    }));

    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);

    QVERIFY(lineEdit->isEnabled());

    g_settings->sync();
    g_settings->beginGroup("programme");
    const QStringList keys = g_settings->childKeys();
    g_settings->endGroup();
    QCOMPARE(keys.size(), 1);

    g_settings->beginGroup("programme");
    const QByteArray storedData = g_settings->value(keys.first()).toByteArray();
    g_settings->endGroup();
    const QByteArray decodedData = QByteArray::fromBase64(storedData);
    const QJsonDocument doc = QJsonDocument::fromJson(decodedData);
    QVERIFY(doc.isObject());
    const QJsonObject obj = doc.object();
    QCOMPARE(obj.value(QStringLiteral("name")).toString(), QStringLiteral("CCTV-4K"));
    QCOMPARE(obj.value(QStringLiteral("itemid")).toString(), QStringLiteral("import-persist-guid"));
    QCOMPARE(obj.value(QStringLiteral("columnid")).toString(), QStringLiteral("import-persist-guid"));
}

void CoreRegressionTests::importDialog_duplicateImport_doesNotAddExtraEntry()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    const QUrl url(QStringLiteral("https://tv.cctv.com/cctv4k/test-dup.shtml"));
    const QByteArray html = R"(
<html><head><script>
var guid = 'dup-test-guid';
</script></head></html>
)";
    manager.queueSuccess(url, html);
    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);

    initializeSettingsSandbox();

    Import importDialog(nullptr);
    auto* lineEdit = importDialog.findChild<QLineEdit*>("lineEdit");
    QVERIFY(lineEdit != nullptr);
    lineEdit->setText(url.toString());

    QSignalSpy resolvedSpy(&apiService, &APIService::playColumnInfoResolved);
    importDialog.ImportProgrammeFromUrl();

    QVERIFY(resolvedSpy.wait(1000));
    QCOMPARE(resolvedSpy.count(), 1);

    const QList<QVariant> firstArgs = resolvedSpy.takeFirst();
    const quint64 firstRequestId = firstArgs.at(0).toULongLong();
    const QStringList firstData = firstArgs.at(1).toStringList();

    g_settings->sync();
    g_settings->beginGroup("programme");
    QCOMPARE(g_settings->childKeys().size(), 1);
    g_settings->endGroup();

    // Call handlePlayColumnInfoResolved directly with the matching
    // request ID to exercise the duplicate-detection branch.
    importDialog.handlePlayColumnInfoResolved(firstRequestId, firstData);

    g_settings->sync();
    g_settings->beginGroup("programme");
    QCOMPARE(g_settings->childKeys().size(), 1);
    g_settings->endGroup();

    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::importDialog_failurePath_resetsBusyState()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    const QUrl url(QStringLiteral("https://tv.cctv.com/fail-test.shtml"));

    manager.queueError(url, QNetworkReply::ContentNotFoundError, QStringLiteral("404 not found"));
    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);

    initializeSettingsSandbox();

    Import importDialog(nullptr);
    auto* lineEdit = importDialog.findChild<QLineEdit*>("lineEdit");
    auto* buttonBox = importDialog.findChild<QDialogButtonBox*>("buttonBox");
    QVERIFY(lineEdit != nullptr);
    QVERIFY(buttonBox != nullptr);
    lineEdit->setText(url.toString());

    QSignalSpy failedSpy(&apiService, &APIService::playColumnInfoFailed);
    importDialog.ImportProgrammeFromUrl();

    QVERIFY(!lineEdit->isEnabled());
    QVERIFY(!buttonBox->isEnabled());

    QVERIFY(failedSpy.wait(1000));
    QCOMPARE(failedSpy.count(), 1);

    QVERIFY(lineEdit->isEnabled());
    QVERIFY(buttonBox->isEnabled());

    g_settings->sync();
    g_settings->beginGroup("programme");
    QCOMPARE(g_settings->childKeys().size(), 0);
    g_settings->endGroup();

    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::importDialog_staleRequestIdIgnored_resolveDoesNotPersist()
{
    initializeSettingsSandbox();

    Import importDialog(nullptr);
    auto* lineEdit = importDialog.findChild<QLineEdit*>("lineEdit");
    QVERIFY(lineEdit != nullptr);

    // m_pendingPlayColumnInfoRequestId starts at 0; requestId != 0
    // should be filtered out by the request-ID guard.
    const QStringList data{
        QStringLiteral("StaleName"),
        QStringLiteral("stale-guid"),
        QStringLiteral("stale-column")
    };
    importDialog.handlePlayColumnInfoResolved(42, data);

    QVERIFY(lineEdit->isEnabled());

    g_settings->sync();
    g_settings->beginGroup("programme");
    QCOMPARE(g_settings->childKeys().size(), 0);
    g_settings->endGroup();
}

void CoreRegressionTests::importDialog_staleRequestIdIgnored_failureDoesNotResetBusy()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    const QUrl url(QStringLiteral("https://tv.cctv.com/stale-busy.shtml"));
    const QByteArray html = R"(
<html><head><script>
var guid = 'stale-busy-guid';
</script></head></html>
)";
    manager.queueSuccess(url, html);
    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);

    initializeSettingsSandbox();

    Import importDialog(nullptr);
    auto* lineEdit = importDialog.findChild<QLineEdit*>("lineEdit");
    auto* buttonBox = importDialog.findChild<QDialogButtonBox*>("buttonBox");
    QVERIFY(lineEdit != nullptr);
    QVERIFY(buttonBox != nullptr);
    lineEdit->setText(url.toString());

    importDialog.ImportProgrammeFromUrl();
    QVERIFY(!lineEdit->isEnabled());
    QVERIFY(!buttonBox->isEnabled());

    // Stale failure notification with mismatched request ID:
    // handler should return early without calling setBusy(false).
    importDialog.handlePlayColumnInfoFailed(999, QStringLiteral("stale error message"));
    QVERIFY(!lineEdit->isEnabled());
    QVERIFY(!buttonBox->isEnabled());

    QSignalSpy resolvedSpy(&apiService, &APIService::playColumnInfoResolved);
    QVERIFY(resolvedSpy.wait(1000));
    QCOMPARE(resolvedSpy.count(), 1);

    QVERIFY(lineEdit->isEnabled());
    QVERIFY(buttonBox->isEnabled());

    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

void CoreRegressionTests::importDialog_asyncClose_closesOnlyAfterPersistence()
{
    APIService& apiService = APIService::instance();
    FakeNetworkAccessManager manager;
    const QUrl url(QStringLiteral("https://tv.cctv.com/cctv4k/test-close-persist.shtml"));
    const QByteArray html = R"(
<html><head><script>
var guid = 'close-persist-guid';
</script></head></html>
)";
    manager.queueSuccess(url, html);
    APIServiceTestAdapter::setTestNetworkAccessManager(apiService, &manager);

    initializeSettingsSandbox();

    Import importDialog(nullptr);
    auto* lineEdit = importDialog.findChild<QLineEdit*>("lineEdit");
    QVERIFY(lineEdit != nullptr);
    lineEdit->setText(url.toString());

    // Dialog should NOT be in Accepted state before OK is clicked
    QCOMPARE(importDialog.result(), QDialog::Rejected);

    QSignalSpy resolvedSpy(&apiService, &APIService::playColumnInfoResolved);
    importDialog.ImportProgrammeFromUrl();

    // Dialog should NOT be in Accepted state while async request is in flight
    QCOMPARE(importDialog.result(), QDialog::Rejected);

    QVERIFY(resolvedSpy.wait(1000));
    QCOMPARE(resolvedSpy.count(), 1);

    // Verify data was persisted
    g_settings->sync();
    g_settings->beginGroup("programme");
    const QStringList keys = g_settings->childKeys();
    g_settings->endGroup();
    QCOMPARE(keys.size(), 1);

    // Dialog should be in Accepted state after successful persistence
    QCOMPARE(importDialog.result(), QDialog::Accepted);

    QCOMPARE(manager.requestCount(), 1);
    QCOMPARE(manager.unexpectedRequestCount(), 0);
}

QTEST_MAIN(CoreRegressionTests)

#include "regression_core_tests.moc"
