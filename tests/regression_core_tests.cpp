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
#include <QCheckBox>
#include <QRadioButton>
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
#include "downloaddialog.h"
#include "decryptworker.h"
#include "concatworker.h"
#include "tsmerger.h"
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
    void decryptWorker_crashExitWithZeroExitCode_emitsProcessFailure();

    void concatWorker_zeroByteFile_emitsFailure();

    void tsMerger_validMinimalPacket_succeeds();
    void tsMerger_zeroByteFile_returnsFalse();
    void tsMerger_malformedNonZeroFile_returnsFalse();
    void tsMerger_failedMerge_preservesExistingOutputFile();

    void apiservice_parseJsonObject_returnsEmptyOnInvalidJson();
    void apiservice_parseJsonArray_missingObjectOrArrayKey_returnsEmptyArray();
    void apiservice_processMonthData_skipsItemsWithoutGuidOrTitle();
    void apiservice_processMonthData_marksHighlightItems();
    void apiservice_processTopicVideoData_marksFragments();
    void apiservice_parseM3U8QualityUrls_and_selectQuality_chooseHighestForZero();
    void apiservice_getPlayColumnInfo_usesGuidFallbackForCctv4k();
    void apiservice_getVideoList_usesCctv4kGuidFallback();
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
    const QString outputPath = QDir(taskDir).filePath(QStringLiteral("result.mp4"));

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
    QVERIFY(createEmptyFile(QDir(tempTaskPath).filePath("result.mp4")));

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
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("重命名MP4->CBOX失败"));
}

void CoreRegressionTests::decryptWorker_fakeProcessRunner_seamSkipsRealProcess()
{
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
    QVERIFY(createEmptyFile(QDir(tempTaskPath).filePath("result.mp4")));

    bool runnerCalled = false;
    DecryptProcessRequest capturedRequest;
    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [&](const DecryptProcessRequest& request) -> DecryptProcessResult {
        runnerCalled = true;
        capturedRequest = request;

        QFile outputFile(request.arguments.at(1));
        if (outputFile.open(QIODevice::WriteOnly)) {
            outputFile.write("fake decrypted bytes");
            outputFile.close();
        }

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
    QCOMPARE(capturedRequest.arguments.at(1), QDir(savePath).filePath("result.mp4"));
    QVERIFY(QFileInfo::exists(QDir(savePath).filePath("fake-runner-video.mp4")));

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("解密完成，输出 fake-runner-video"));
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
    DecryptWorkerTestAdapter::setProcessTimeoutMs(worker, 0);
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString taskHash = decryptTaskHash(name);
    const QString tempTaskPath = QDir(savePath).filePath(taskHash);
    QVERIFY(QDir().mkpath(tempTaskPath));
    QVERIFY(createEmptyFile(QDir(tempTaskPath).filePath("result.mp4")));

    int seenTimeoutMs = -1;
    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [&](const DecryptProcessRequest& request) -> DecryptProcessResult {
        seenTimeoutMs = request.timeoutMs;

        QFile outputFile(request.arguments.at(1));
        if (outputFile.open(QIODevice::WriteOnly)) {
            outputFile.write("fake decrypted bytes");
            outputFile.close();
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
    QCOMPARE(seenTimeoutMs, 30000);

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
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString taskHash = decryptTaskHash(name);
    const QString tempTaskPath = QDir(savePath).filePath(taskHash);
    QVERIFY(QDir().mkpath(tempTaskPath));
    QVERIFY(createEmptyFile(QDir(tempTaskPath).filePath("result.mp4")));

    DecryptProcessRequest capturedRequest;
    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [&](const DecryptProcessRequest& request) -> DecryptProcessResult {
        capturedRequest = request;

        QFile outputFile(request.arguments.at(1));
        if (outputFile.open(QIODevice::WriteOnly)) {
            outputFile.write("fake decrypted bytes");
            outputFile.close();
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
    QCOMPARE(capturedRequest.program, QDir(decryptAssetsDir.path()).filePath("cbox.exe"));
    QVERIFY(QFileInfo::exists(QDir(savePath).filePath("assets-dir-video.mp4")));
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
    QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("解密失败 [code=input_missing]: result.mp4 不存在"));
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
    QVERIFY(createEmptyFile(QDir(tempTaskPath).filePath("result.mp4")));

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
    const QString mp4Path = QDir(tempTaskPath).filePath("result.mp4");
    QVERIFY(createEmptyFile(mp4Path));

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
    QVERIFY(createEmptyFile(QDir(tempTaskPath).filePath("result.mp4")));

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
    QVERIFY(createEmptyFile(QDir(tempTaskPath).filePath("result.mp4")));

    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [](const DecryptProcessRequest&) {
        DecryptProcessResult result;
        result.errorString = QStringLiteral("synthetic launch failure");
        return result;
    });

    worker.doDecrypt();

    DecryptWorkerTestAdapter::clearTestProcessRunner(worker);
    DecryptWorkerTestAdapter::clearTestDecryptAssetsDir(worker);

    QCOMPARE(spy.count(), 1);
    QVERIFY(QFileInfo::exists(QDir(tempTaskPath).filePath("result.mp4")));
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
    const QString resultMp4Path = QDir(tempTaskPath).filePath("result.mp4");
    const QString inputCboxPath = QDir(tempTaskPath).filePath("input.cbox");
    QVERIFY(createEmptyFile(resultMp4Path));

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
    QVERIFY(createEmptyFile(QDir(tempTaskPath).filePath("result.mp4")));

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
    QVERIFY(QFileInfo::exists(QDir(tempTaskPath).filePath("result.mp4")));
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
    QVERIFY(createEmptyFile(QDir(tempTaskPath).filePath("result.mp4")));

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
    QVERIFY(createEmptyFile(QDir(tempTaskPath).filePath("result.mp4")));

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
    const QString resultMp4Path = QDir(tempTaskPath).filePath("result.mp4");
    const QString inputCboxPath = QDir(tempTaskPath).filePath("input.cbox");
    QVERIFY(createEmptyFile(resultMp4Path));

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
    const QString resultMp4Path = QDir(tempTaskPath).filePath("result.mp4");
    const QString inputCboxPath = QDir(tempTaskPath).filePath("input.cbox");
    QVERIFY(createEmptyFile(resultMp4Path));

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
    DecryptWorkerTestAdapter::setTestDecryptAssetsDir(worker, decryptAssetsDir.path());

    const QString tempTaskPath = QDir(savePath).filePath(decryptTaskHash(name));
    QVERIFY(QDir().mkpath(tempTaskPath));
    QVERIFY(createEmptyFile(QDir(tempTaskPath).filePath("result.mp4")));

    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [](const DecryptProcessRequest& request) {
        QFile outputFile(request.arguments.at(1));
        if (outputFile.open(QIODevice::WriteOnly)) {
            outputFile.write("fake decrypted bytes");
            outputFile.close();
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
    QVERIFY(QFileInfo::exists(QDir(savePath).filePath("success-preexisting-license-video.mp4")));
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
    QVERIFY(createEmptyFile(QDir(tempTaskPath).filePath("result.mp4")));

    DecryptWorkerTestAdapter::setTestProcessRunner(worker, [&cboxOutputFileName](const DecryptProcessRequest& request) {
        cboxOutputFileName = QFileInfo(request.arguments.at(1)).fileName();
        QFile outputFile(request.arguments.at(1));
        if (outputFile.open(QIODevice::WriteOnly)) {
            outputFile.write("fake decrypted ts bytes");
            outputFile.close();
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
    QCOMPARE(cboxOutputFileName, QString("result.ts"));
    QVERIFY(QFileInfo::exists(QDir(savePath).filePath("keep-ts-video.ts")));
    QVERIFY(!QFileInfo::exists(QDir(savePath).filePath("keep-ts-video.mp4")));

    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
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
    const QString resultMp4Path = QDir(tempTaskPath).filePath("result.mp4");
    const QString inputCboxPath = QDir(tempTaskPath).filePath("input.cbox");
    QVERIFY(createEmptyFile(resultMp4Path));

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

    const auto tsUrls = apiService.getEncryptM3U8Urls(guid, QStringLiteral("0"));

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
}

void CoreRegressionTests::tsMerger_zeroByteFile_returnsFalse()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString zeroFile = QDir(tempDir.path()).filePath("0000.ts");
    QVERIFY(createEmptyFile(zeroFile));

    const QString outputPath = QDir(tempDir.path()).filePath("result.mp4");

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

    const QString outputPath = QDir(tempDir.path()).filePath("result.mp4");

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

    const QString outputPath = QDir(tempDir.path()).filePath("result.mp4");
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
    {
        QFile file(validFile);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QByteArray packet(188, '\0');
        packet[0] = static_cast<char>(0x47); // TS sync byte
        QCOMPARE(file.write(packet), 188);
    }

    const QString outputPath = QDir(tempDir.path()).filePath("result.mp4");

    TSMerger merger;
    merger.reset();

    const std::vector<QString> files = {validFile};
    QVERIFY(merger.merge(files, outputPath));
    QVERIFY(QFileInfo::exists(outputPath));
    QVERIFY(QFileInfo(outputPath).size() > 0);
}

QTEST_MAIN(CoreRegressionTests)

#include "regression_core_tests.moc"
