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
#include <memory>
#include <tuple>

#include "config.h"
#include "setting.h"
#include "apiservice.h"
#include "downloadtask.h"
#include "decryptworker.h"

class APIServiceTestAdapter {
public:
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
    void decryptWorker_renameFailure_emitsRenameError();

    void apiservice_parseJsonObject_returnsEmptyOnInvalidJson();
    void apiservice_parseJsonArray_missingObjectOrArrayKey_returnsEmptyArray();
    void apiservice_processMonthData_skipsItemsWithoutGuidOrTitle();
    void apiservice_parseM3U8QualityUrls_and_selectQuality_chooseHighestForZero();
    void apiservice_buildVideoApiUrl_buildsExpectedQuery();
    void apiservice_buildTsUrlsFromPlaylistData_returnsExpectedAbsoluteUrls();
    void apiservice_getTsFileList_returnsExpectedUrlsFromSyntheticData();

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
    APIService::s_testM3u8Response.clear();
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

void CoreRegressionTests::apiservice_getTsFileList_returnsExpectedUrlsFromSyntheticData()
{
    APIService& apiService = APIService::instance();

    const QByteArray syntheticPlaylist = R"(
#EXTM3U
#EXTINF:2.0,
segment-0001.ts
#EXTINF:2.0,
segment-0002.ts
)";
    APIService::s_testM3u8Response = syntheticPlaylist;

    const QString qualityPath = QString("/asp/enc/video-123/index.m3u8");
    const QString baseUrl = QString("https://dh5.example.com/asp/enc/video-123/index.m3u8");
    const auto tsUrls = APIServiceTestAdapter::getTsFileList(apiService, qualityPath, baseUrl);

    QCOMPARE(tsUrls.size(), 2);
    QCOMPARE(tsUrls.at(0), QString("https://dh5.example.com/asp/enc/video-123/segment-0001.ts"));
    QCOMPARE(tsUrls.at(1), QString("https://dh5.example.com/asp/enc/video-123/segment-0002.ts"));

    APIService::s_testM3u8Response.clear();
}

QTEST_MAIN(CoreRegressionTests)

#include "regression_core_tests.moc"
