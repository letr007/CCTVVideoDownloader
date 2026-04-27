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
#include <QSpinBox>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QFile>
#include <QElapsedTimer>
#include <QTimer>
#include <functional>
#include <memory>
#include <tuple>

#include "config.h"
#include "setting.h"
#include "apiservice.h"
#include "downloadengine.h"
#include "downloadtask.h"
#include "decryptworker.h"
#include "fakes/fake_networkaccessmanager.h"
#include "fakes/fake_networkreply.h"

namespace {

QString decryptTaskHash(const QString& name)
{
    return QString(QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256).toHex());
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

void createDecryptAssets(const QString& assetsDirPath, bool includeCboxExe = true, bool includeLicense = true)
{
    if (includeCboxExe) {
        QVERIFY(createEmptyFile(QDir(assetsDirPath).filePath("cbox.exe")));
    }
    if (includeLicense) {
        QVERIFY(createEmptyFile(QDir(assetsDirPath).filePath("UDRM_LICENSE.v1.0")));
    }
}

}

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
    static void setTestReplyFactory(DownloadEngine& engine, const std::function<QNetworkReply*(const QNetworkRequest&)>& replyFactory)
    {
        engine.setTestReplyFactory(replyFactory);
    }

    static void clearTestReplyFactory(DownloadEngine& engine)
    {
        engine.clearTestReplyFactory();
    }
};

class DecryptWorkerTestAdapter {
public:
    static void setProcessTimeoutMs(DecryptWorker& worker, int timeoutMs)
    {
        worker.setProcessTimeoutMs(timeoutMs);
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
    void downloadTask_cancelWhileReplyPending_withTimeoutEnabled_preservesCancellation();
    void downloadTask_retryOnNetworkError_thenSucceeds_writesOnlyFinalBytes();
    void downloadTask_retryOnNetworkError_exhausted_emitsStructuredFailure();
    void downloadTask_retryOnTimeout_exhausted_emitsStructuredTimeoutFailure();
    void downloadTask_cancelDuringRetryDelay_doesNotIssueSecondRequest();
    void downloadEngine_idleConstructionDestruction_isSafe();
    void downloadEngine_fakeNetwork_success_emitsDownloadAndAllFinished();
    void downloadEngine_fakeNetwork_error_emitsFailureAndAllFinished();
    void downloadEngine_cancelActiveTask_emitsSingleCancelledFailureAndAllFinished();
    void downloadEngine_cancelWhenIdle_isSafeNoOp();
    void downloadEngine_retrySettings_propagateToTask();

    void decryptWorker_renameFailure_emitsRenameError();
    void decryptWorker_missingLicense_emitsPreflightErrorBeforeProcessStart();

    void apiservice_parseJsonObject_returnsEmptyOnInvalidJson();
    void apiservice_parseJsonArray_missingObjectOrArrayKey_returnsEmptyArray();
    void apiservice_processMonthData_skipsItemsWithoutGuidOrTitle();
    void apiservice_parseM3U8QualityUrls_and_selectQuality_chooseHighestForZero();
    void apiservice_buildVideoApiUrl_buildsExpectedQuery();
    void apiservice_buildTsUrlsFromPlaylistData_returnsExpectedAbsoluteUrls();
    void apiservice_sendNetworkRequest_fakeSuccess_returnsDeterministicBody();
    void apiservice_sendNetworkRequest_fakeError_returnsEmptyData();
    void apiservice_getTsFileList_returnsExpectedUrlsFromSyntheticData();

    void fakeNetworkReply_success_emitsReadyReadProgressAndFinished();
    void fakeNetworkReply_abort_marksCancelledAndFinishes();
    void fakeNetworkAccessManager_queueErrorAndUnexpectedRequestFailDeterministically();
    void fakeNetworkAccessManager_delayedFinish_waitsUntilQueuedCompletion();

private:
    void initializeSettingsSandbox();

    QString m_originalCurrentPath;
    std::unique_ptr<QTemporaryDir> m_tempDir;
};

void CoreRegressionTests::init()
{
    m_originalCurrentPath = QDir::currentPath();
    m_tempDir = std::make_unique<QTemporaryDir>();
    QVERIFY2(m_tempDir->isValid(), "Temporary directory must be valid");
    QVERIFY(QDir::setCurrent(m_tempDir->path()));
    g_settings.reset();
}

void CoreRegressionTests::cleanup()
{
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
    QCOMPARE(readThreadNum(), 10);
    QCOMPARE(readQuality(), QString("1"));
    QCOMPARE(readLogLevel(), 1);
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

    QVERIFY(savePathEdit != nullptr);
    QVERIFY(threadSpin != nullptr);
    QVERIFY(dateBegEdit != nullptr);
    QVERIFY(dateEndEdit != nullptr);
    QVERIFY(qualityCombo != nullptr);
    QVERIFY(logCombo != nullptr);

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

    setting.saveSettings();

    const auto [dateBeg, dateEnd] = readDisplayMinAndMax();

    QCOMPARE(readSavePath(), expectedSavePath);
    QCOMPARE(readThreadNum(), expectedThreadNum);
    QCOMPARE(readQuality(), QString::number(expectedQuality));
    QCOMPARE(readLogLevel(), expectedLogLevel);
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

        QVERIFY(savePathEdit != nullptr);
        QVERIFY(threadSpin != nullptr);
        QVERIFY(dateBegEdit != nullptr);
        QVERIFY(dateEndEdit != nullptr);
        QVERIFY(qualityCombo != nullptr);
        QVERIFY(logCombo != nullptr);

        savePathEdit->setText(expectedSavePath);
        threadSpin->setValue(expectedThreadNum);
        dateBegEdit->setDate(expectedDateBeg);
        dateEndEdit->setDate(expectedDateEnd);
        qualityCombo->setCurrentIndex(expectedQuality);
        logCombo->setCurrentIndex(expectedLogLevel);

        setting.saveSettings();
    }

    g_settings.reset();
    initGlobalSettings();

    const auto [dateBeg, dateEnd] = readDisplayMinAndMax();

    QCOMPARE(readSavePath(), expectedSavePath);
    QCOMPARE(readThreadNum(), expectedThreadNum);
    QCOMPARE(readQuality(), QString::number(expectedQuality));
    QCOMPARE(readLogLevel(), expectedLogLevel);
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
    const QByteArray expectedBody("timeout fake body");

    manager.queueSuccess(url, expectedBody, 1000);

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

    manager.queueSuccess(url, QByteArray("timeout-attempt-one"), 1000);
    manager.queueSuccess(url, QByteArray("timeout-attempt-two"), 1000);

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

void CoreRegressionTests::decryptWorker_renameFailure_emitsRenameError()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_rename_failure");
    QVERIFY(QDir().mkpath(savePath));

    const QString name = QString("unit-video");
    worker.setParams(name, savePath);
    const QString taskHash = QString(QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256).toHex());
    const QString tempTaskPath = QDir(savePath).filePath(taskHash);
    QVERIFY(QDir().mkpath(tempTaskPath));

    worker.doDecrypt();

    QCOMPARE(spy.count(), 1);

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("重命名MP4->CBOX失败"));
}

void CoreRegressionTests::decryptWorker_missingLicense_emitsPreflightErrorBeforeProcessStart()
{
    DecryptWorker worker;
    QSignalSpy spy(&worker, &DecryptWorker::decryptFinished);

    const QString savePath = QDir(m_tempDir->path()).filePath("decrypt_missing_license");
    QVERIFY(QDir().mkpath(savePath));

    const QString name = QString("license-video");
    worker.setParams(name, savePath);

    const QString taskHash = QString(QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256).toHex());
    const QString tempTaskPath = QDir(savePath).filePath(taskHash);
    QVERIFY(QDir().mkpath(tempTaskPath));
    const QString mp4Path = QDir(tempTaskPath).filePath("result.mp4");
    QFile mp4File(mp4Path);
    QVERIFY(mp4File.open(QIODevice::WriteOnly));
    mp4File.close();

    worker.doDecrypt();

    QCOMPARE(spy.count(), 1);
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("解密所需的许可证文件不存在"));
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
)";

    const auto qualityUrls = APIServiceTestAdapter::parseM3U8QualityUrls(apiService, m3u8Payload, QString("https://dh5.example/asp/enc2/index.m3u8"));

    QCOMPARE(qualityUrls.size(), 3);
    QCOMPARE(qualityUrls.value(QString("4")), QString("low.m3u8"));
    QCOMPARE(qualityUrls.value(QString("2")), QString("mid.m3u8"));
    QCOMPARE(qualityUrls.value(QString("1")), QString("high.m3u8"));

    const auto selected = APIServiceTestAdapter::selectQuality(apiService, QString("0"), qualityUrls);
    QCOMPARE(selected, QString("1"));
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

void CoreRegressionTests::apiservice_buildTsUrlsFromPlaylistData_returnsExpectedAbsoluteUrls()
{
    APIService& apiService = APIService::instance();
    const QByteArray playlistData = R"(
#EXTM3U
#EXTINF:2.0,
segment-0001.ts
#EXTINF:2.0,
segment-0002.ts
)";

    const auto tsUrls = APIServiceTestAdapter::buildTsUrlsFromPlaylistData(apiService, playlistData, QString("https://example.com/path/video/index.m3u8"));

    QCOMPARE(tsUrls.size(), 2);
    QCOMPARE(tsUrls.at(0), QString("https://example.com/path/video/segment-0001.ts"));
    QCOMPARE(tsUrls.at(1), QString("https://example.com/path/video/segment-0002.ts"));
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

QTEST_MAIN(CoreRegressionTests)

#include "regression_core_tests.moc"
