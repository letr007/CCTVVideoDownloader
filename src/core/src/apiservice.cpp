#include "../../include/apiservice.h"
#include "../include/contentresolver.h"
#include <QCoreApplication>
#include <algorithm>
#include <QDate>
#include <QDateTime>
#include <QTimeZone>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QSslConfiguration>
#include <QtNetwork/QSslSocket>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QMutexLocker>
#include <QMap>
#include <cmath>
#include <limits>
#include <utility>

// 静态成员初始化
QPointer<APIService> APIService::m_instance = nullptr;
QMutex APIService::m_instanceMutex;

namespace {

QString formatPublishedTime(const QJsonValue& value)
{
    if (value.isDouble()) {
        return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(value.toDouble()), QTimeZone("Asia/Shanghai"))
            .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }

    const QString text = value.toString().trimmed();
    bool millisecondsOk = false;
    const qint64 milliseconds = text.toLongLong(&millisecondsOk);
    if (millisecondsOk) {
        return QDateTime::fromMSecsSinceEpoch(milliseconds, QTimeZone("Asia/Shanghai"))
            .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }
    return text;
}

QDate parsePublishedDate(const QJsonValue& value)
{
    const QString dateText = formatPublishedTime(value);
    QDate date = QDate::fromString(dateText.left(10), QStringLiteral("yyyy-MM-dd"));
    if (date.isValid()) {
        return date;
    }
    return QDate::fromString(dateText.left(8), QStringLiteral("yyyyMMdd"));
}

QString readVideoChannel(const QJsonObject& object)
{
    const QString channel = object.value(QStringLiteral("channel")).toString().trimmed();
    if (!channel.isEmpty()) {
        return channel;
    }
    return object.value(QStringLiteral("play_channel")).toString().trimmed();
}

bool parsePublishedTimestamp(const QJsonValue& value, qint64& timestamp)
{
    if (value.isDouble()) {
        const double milliseconds = value.toDouble();
        if (!std::isfinite(milliseconds)
            || milliseconds < static_cast<double>(std::numeric_limits<qint64>::min())
            || milliseconds > static_cast<double>(std::numeric_limits<qint64>::max())) {
            return false;
        }
        timestamp = static_cast<qint64>(milliseconds);
        return QDateTime::fromMSecsSinceEpoch(timestamp, QTimeZone("Asia/Shanghai")).date().isValid();
    }

    const QString text = value.toString().trimmed();
    bool millisecondsOk = false;
    const qint64 milliseconds = text.toLongLong(&millisecondsOk);
    if (millisecondsOk) {
        timestamp = milliseconds;
        return QDateTime::fromMSecsSinceEpoch(timestamp, QTimeZone("Asia/Shanghai")).date().isValid();
    }

    const QDate published = parsePublishedDate(value);
    if (!published.isValid()) {
        return false;
    }

    QDateTime dateTime = QDateTime::fromString(text, Qt::ISODate);
    if (!dateTime.isValid()) {
        dateTime = QDateTime::fromString(text, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }
    if (!dateTime.isValid()) {
        dateTime = QDateTime(published, QTime(0, 0), QTimeZone("Asia/Shanghai"));
    }
    timestamp = dateTime.toMSecsSinceEpoch();
    return true;
}

qint64 parseDurationSeconds(const QJsonValue& value)
{
    if (value.isDouble()) {
        const double seconds = value.toDouble(-1.0);
        return seconds >= 0.0 ? static_cast<qint64>(seconds) : -1;
    }

    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return -1;
    }

    const QStringList parts = text.split(QLatin1Char(':'));
    if (parts.size() == 2 || parts.size() == 3) {
        bool hoursOk = true;
        bool minutesOk = false;
        bool secondsOk = false;
        const qint64 hours = parts.size() == 3 ? parts[0].toLongLong(&hoursOk) : 0;
        const qint64 minutes = parts[parts.size() - 2].toLongLong(&minutesOk);
        const double seconds = parts.last().toDouble(&secondsOk);
        if (hoursOk && minutesOk && secondsOk && hours >= 0 && minutes >= 0 && seconds >= 0.0) {
            return hours * 3600 + minutes * 60 + static_cast<qint64>(seconds);
        }
        return -1;
    }

    bool ok = false;
    const double seconds = text.toDouble(&ok);
    return ok && seconds >= 0.0 ? static_cast<qint64>(seconds) : -1;
}

void appendUniqueVideos(QMap<int, VideoItem>& videos, const QMap<int, VideoItem>& extras)
{
    int nextIndex = videos.isEmpty() ? 0 : (videos.lastKey() + 1);
    for (const VideoItem& item : extras) {
        const bool alreadyListed = std::any_of(videos.cbegin(), videos.cend(), [&item](const VideoItem& existing) {
            return !item.guid.isEmpty() && item.guid == existing.guid;
        });
        if (!alreadyListed) {
            videos.insert(nextIndex++, item);
        }
    }
}

} // namespace

APIService& APIService::instance() {
    if (m_instance.isNull()) {
        QMutexLocker locker(&m_instanceMutex);
        if (m_instance.isNull()) {
            m_instance = new APIService(qApp);
        }
    }
    return *m_instance;
}

APIService::APIService(QObject* parent) : QObject(parent)
{
    // connect(&m_network, &NetworkCore::responseReceived, this, &APIService::handlePlayColumnInfo, Qt::QueuedConnection);
}

APIService::~APIService()
{
}

QNetworkAccessManager* APIService::networkAccessManager()
{
#ifdef CORE_REGRESSION_TESTS
    if (m_testNetworkAccessManager) {
        return m_testNetworkAccessManager;
    }
#endif
    return &m_networkAccessManager;
}

QNetworkAccessManager* APIService::callScopedNetworkAccessManager(QNetworkAccessManager& localManager)
{
#ifdef CORE_REGRESSION_TESTS
    if (m_testNetworkAccessManager) {
        return m_testNetworkAccessManager;
    }
    if (m_testCallScopedNetworkAccessManagerFactory) {
        return m_testCallScopedNetworkAccessManagerFactory();
    }
#endif
    return &localManager;
}

QNetworkRequest APIService::buildNetworkRequest(const QUrl& url, const QHash<QString, QString>& headers) const
{
    QNetworkRequest request(url);

    QSslConfiguration sslConfig = request.sslConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    if (url.scheme() == QStringLiteral("https")
        && url.host() == QStringLiteral("media.app.cctv.com")
        && url.port(443) == 443
        && url.path() == QStringLiteral("/vapi/video/vplist.do")) {
        sslConfig.setSslOption(QSsl::SslOptionDisableLegacyRenegotiation, false);
    }
    request.setSslConfiguration(sslConfig);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Lavf/60.10.100");

    for (auto it = headers.begin(); it != headers.end(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    return request;
}

// 通用的网络请求函数
QByteArray APIService::sendNetworkRequest(const QUrl& url, const QHash<QString, QString>& headers)
{
    QNetworkAccessManager localManager;
    QNetworkAccessManager* manager = &localManager;
#ifdef CORE_REGRESSION_TESTS
    if (m_testNetworkAccessManager) {
        manager = m_testNetworkAccessManager;
    }
#endif
    return sendNetworkRequest(manager, url, headers);
}

QByteArray APIService::sendNetworkRequest(QNetworkAccessManager* networkAccessManager,
    const QUrl& url,
    const QHash<QString, QString>& headers)
{
    qInfo() << "发送网络请求:" << url.toString();

    QNetworkRequest request = buildNetworkRequest(url, headers);

    QNetworkReply* reply = networkAccessManager->get(request);
    // 连接SSL错误处理，忽略SSL错误
    QObject::connect(reply, &QNetworkReply::errorOccurred,
        [reply](QNetworkReply::NetworkError error) {
            if (error == QNetworkReply::SslHandshakeFailedError) {
                qWarning() << "SSL握手失败，尝试忽略错误:" << reply->errorString();
                reply->ignoreSslErrors();
            }
        });
    
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "网络请求失败:" << reply->errorString() << "URL:" << url.toString();
        reply->deleteLater();
        return QByteArray();
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    if (responseData.isEmpty()) {
        qWarning() << "从URL获取的响应数据为空:" << url.toString();
    } else {
        qInfo() << "网络请求成功，响应数据大小:" << responseData.size() << "字节";
    }

    return responseData;
}

#ifdef CORE_REGRESSION_TESTS
void APIService::setTestNetworkAccessManager(QNetworkAccessManager* networkAccessManager)
{
    m_testNetworkAccessManager = networkAccessManager;
}

void APIService::clearTestNetworkAccessManager()
{
    m_testNetworkAccessManager = nullptr;
}

void APIService::setTestCallScopedNetworkAccessManagerFactory(
    std::function<QNetworkAccessManager*()> networkAccessManagerFactory)
{
    m_testCallScopedNetworkAccessManagerFactory = std::move(networkAccessManagerFactory);
}

void APIService::clearTestCallScopedNetworkAccessManagerFactory()
{
    m_testCallScopedNetworkAccessManagerFactory = {};
}
#endif

QByteArray APIService::fetchPageHtml(const QUrl& url)
{
    return sendNetworkRequest(url);
}

QMap<int, VideoItem> APIService::fetchSingleVideoByGuid(const QString& serviceId, const QString& guid)
{
    QMap<int, VideoItem> result;
    QUrl videoInfoUrl(QStringLiteral("https://zy.api.cntv.cn/video/videoinfoByGuid"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("serviceId"), serviceId);
    query.addQueryItem(QStringLiteral("guid"), guid);
    videoInfoUrl.setQuery(query);

    const QByteArray responseData = sendNetworkRequest(videoInfoUrl);
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return result;
    }

    const QJsonObject root = doc.object();
    const QJsonObject nestedData = root.value(QStringLiteral("data")).toObject();
    const QJsonObject videoObj = nestedData.isEmpty() ? root : nestedData;
    const QString title = videoObj.value(QStringLiteral("title")).toString();
    if (title.isEmpty()) {
        return result;
    }

    VideoItem videoItem;
    videoItem.guid = videoObj.value(QStringLiteral("vid")).toString();
    videoItem.title = title;
    videoItem.brief = videoObj.value(QStringLiteral("brief")).toString();
    videoItem.image = videoObj.value(QStringLiteral("img")).toString();
    videoItem.time = videoObj.value(QStringLiteral("time")).toString();
    videoItem.channel = readVideoChannel(videoObj);
    videoItem.length = parseDurationSeconds(videoObj.value(QStringLiteral("length")));
    if (videoItem.guid.isEmpty()) {
        videoItem.guid = guid;
    }
    result.insert(0, videoItem);
    return result;
}

QMap<int, VideoItem> APIService::fetchColumnVideoList(const QString& columnId, const QStringList& dates)
{
    return fetchVideoData(columnId, dates, FetchType::Column);
}

QMap<int, VideoItem> APIService::fetchVcctvProgrammeVideoList(const QString& mid, const QString& chid,
    const QString& startDate, const QString& endDate)
{
    QMap<int, VideoItem> result;
    if (mid.isEmpty() || chid.isEmpty()) {
        return result;
    }

    const QDate startMonth = QDate::fromString(startDate + QStringLiteral("01"), QStringLiteral("yyyyMMdd"));
    const QDate endMonth = QDate::fromString(endDate + QStringLiteral("01"), QStringLiteral("yyyyMMdd"));
    if (!startMonth.isValid() || !endMonth.isValid()) {
        return result;
    }
    const QDate latestMonth = qMax(startMonth, endMonth);
    const QDate earliestMonth = qMin(startMonth, endMonth);
    const QDate latestDate = latestMonth.addMonths(1).addDays(-1);

    struct PageData {
        QJsonArray items;
        QDate newestDate;
        QDate oldestDate;
        qint64 newestTimestamp = 0;
        qint64 oldestTimestamp = 0;
    };

    constexpr int requestedPageSize = 100;
    QNetworkAccessManager localManager;
    QNetworkAccessManager* manager = callScopedNetworkAccessManager(localManager);
    QMap<int, PageData> pageCache;
    int totalPages = 0;
    auto fetchPage = [&](int page) {
        if (pageCache.contains(page)) {
            return true;
        }

        const QByteArray responseData = sendNetworkRequest(manager,
            buildVcctvProgrammeVideoListUrl(mid, chid, page, requestedPageSize));
        if (responseData.isEmpty()) {
            qWarning() << "获取 v.cctv.com 栏目列表失败: 第" << page << "页响应为空";
            return false;
        }

        const QJsonObject root = parseJsonObject(responseData);
        const QJsonArray items = root.value(QStringLiteral("data")).toArray();
        if (items.isEmpty()) {
            qWarning() << "获取 v.cctv.com 栏目列表失败: 第" << page << "页数据为空";
            return false;
        }
        if (page == 1) {
            const QJsonValue countValue = root.value(QStringLiteral("count"));
            bool countOk = countValue.isDouble();
            int total = countValue.isDouble() ? countValue.toInt() : countValue.toString().toInt(&countOk);
            if (!countOk || total <= 0) {
                total = items.size();
            }
            const int actualPageSize = static_cast<int>(items.size());
            totalPages = std::max(1, (total + actualPageSize - 1) / actualPageSize);
        }

        PageData pageData;
        pageData.items = items;
        qint64 previousTimestamp = 0;
        for (int index = 0; index < items.size(); ++index) {
            const QJsonValue pubTimeValue = items.at(index).toObject().value(QStringLiteral("pubTime"));
            qint64 timestamp = 0;
            const QDate published = parsePublishedDate(pubTimeValue);
            if (!published.isValid() || !parsePublishedTimestamp(pubTimeValue, timestamp)) {
                qWarning() << "获取 v.cctv.com 栏目列表失败: 第" << page
                           << "页第" << index << "项 pubTime 无效";
                return false;
            }
            if (index > 0 && timestamp > previousTimestamp) {
                qWarning() << "获取 v.cctv.com 栏目列表失败: 第" << page
                           << "页 pubTime 未按降序排列";
                return false;
            }
            if (index == 0) {
                pageData.newestDate = published;
                pageData.newestTimestamp = timestamp;
            }
            pageData.oldestDate = published;
            pageData.oldestTimestamp = timestamp;
            previousTimestamp = timestamp;
        }

        for (auto cached = pageCache.cbegin(); cached != pageCache.cend(); ++cached) {
            if ((cached.key() < page && cached.value().oldestTimestamp < pageData.newestTimestamp)
                || (cached.key() > page && pageData.oldestTimestamp < cached.value().newestTimestamp)) {
                qWarning() << "获取 v.cctv.com 栏目列表失败: 缓存页范围未按 pubTime 全局降序排列"
                           << "页" << page << "与页" << cached.key();
                return false;
            }
        }

        pageCache.insert(page, pageData);
        QCoreApplication::processEvents();
        return true;
    };

    if (!fetchPage(1)) {
        return {};
    }

    auto findFirstTargetPage = [&]() {
        if (pageCache.value(1).oldestDate <= latestDate) {
            return 1;
        }

        int low = 2;
        int high = totalPages;
        int first = totalPages + 1;
        while (low <= high) {
            const int page = low + (high - low) / 2;
            if (!fetchPage(page)) {
                return 0;
            }
            if (pageCache.value(page).oldestDate <= latestDate) {
                first = page;
                high = page - 1;
            } else {
                low = page + 1;
            }
        }
        return first;
    };

    const int firstTargetPage = findFirstTargetPage();
    if (firstTargetPage == 0 || firstTargetPage > totalPages) {
        return {};
    }

    int resultIndex = 0;
    for (int page = firstTargetPage; page <= totalPages; ++page) {
        if (!fetchPage(page)) {
            return {};
        }
        if (pageCache.value(page).newestDate < earliestMonth) {
            break;
        }

        for (const QJsonValue& value : pageCache.value(page).items) {
            const QJsonObject item = value.toObject();
            const QJsonValue pubTimeValue = item.value(QStringLiteral("pubTime"));
            const QString pubTime = formatPublishedTime(pubTimeValue);
            const QDate published = parsePublishedDate(pubTimeValue);
            const QDate month(published.year(), published.month(), 1);
            if (month < earliestMonth || month > latestMonth) {
                continue;
            }

            const QString guid = item.value(QStringLiteral("guid")).toString();
            const QString title = item.value(QStringLiteral("title")).toString();
            if (guid.isEmpty() || title.isEmpty()) {
                continue;
            }

            VideoItem videoItem;
            videoItem.guid = guid;
            videoItem.title = title;
            videoItem.brief = item.value(QStringLiteral("vbrief")).toString();
            videoItem.image = item.value(QStringLiteral("image1")).toString();
            videoItem.time = pubTime;
            videoItem.length = parseDurationSeconds(item.value(QStringLiteral("vduration")));
            videoItem.channel = item.value(QStringLiteral("mediaName")).toString();
            result.insert(resultIndex++, videoItem);
        }
    }
    return result;
}

QMap<int, VideoItem> APIService::fetchAlbumVideoList(const QString& albumId, int mode)
{
    QMap<int, VideoItem> result;
    if (albumId.isEmpty()) {
        return result;
    }

    int resultIndex = 0;
    constexpr int pageSize = 100;
    QNetworkAccessManager localManager;
    QNetworkAccessManager* manager = callScopedNetworkAccessManager(localManager);
    int page = 1;
    int totalPages = 1;
    do {
        const QByteArray responseData = sendNetworkRequest(manager, buildAlbumVideoListUrl(albumId, mode, page, pageSize));
        if (responseData.isEmpty()) {
            break;
        }

        const QJsonObject dataObj = parseJsonObject(responseData, QStringLiteral("data"));
        const QJsonArray items = dataObj.value(QStringLiteral("list")).toArray();
        if (items.isEmpty()) {
            break;
        }
        if (page == 1) {
            const int total = dataObj.value(QStringLiteral("total")).toInt(items.size());
            totalPages = std::max(1, (total + pageSize - 1) / pageSize);
        }
        processMonthData(items, QStringLiteral("album"), result, resultIndex);
        QCoreApplication::processEvents();
        ++page;
    } while (page <= totalPages);
    return result;
}

QString APIService::resolveAlbumId(const QString& itemId)
{
    return getRealAlbumId(itemId);
}

QSharedPointer<ContentParse::ImportResult> APIService::getPlayColumnInfo(const QString& url)
{
    return ContentParse::ContentResolver(*this).resolvePlayColumnInfo(url);
}

QMap<int, VideoItem> APIService::getVideoList(const ContentParse::ProgrammeRecord& record,
    const QString& startDate,
    const QString& endDate)
{
    return ContentParse::ContentResolver(*this).resolveVideoList(record, startDate, endDate);
}

QMap<int, VideoItem> APIService::getVideoList(const ContentParse::ImportResult& result,
    const QString& startDate,
    const QString& endDate)
{
    return getVideoList(ContentParse::makeProgrammeRecord(result), startDate, endDate);
}

QMap<int, VideoItem> APIService::getVideoList(const QString& columnId,
    const QString& itemId,
    const QString& startDate,
    const QString& endDate)
{
    return ContentParse::ContentResolver(*this).resolveVideoList(columnId, itemId, startDate, endDate);
}


QMap<int, VideoItem> APIService::getHighlightList(const QString& item_id)
{
    qInfo() << "获取节目看点列表，item_id:" << item_id;

    QMap<int, VideoItem> result;
    QNetworkAccessManager localManager;
    QNetworkAccessManager* manager = callScopedNetworkAccessManager(localManager);
    QString real_album_id = getRealAlbumId(item_id, manager);
    if (real_album_id.isEmpty()) {
        qWarning() << "获取节目看点失败: 无法获取真实专辑ID";
        return result;
    }

    constexpr int pageSize = 100;
    int page = 1;
    int totalPages = 1;
    int resultIndex = 0;

    do {
        QUrl url = buildAlbumVideoListUrl(real_album_id, 1, page, pageSize);
        QByteArray responseData = sendNetworkRequest(manager, url);
        if (responseData.isEmpty()) {
            qWarning() << "获取节目看点失败: 第" << page << "页响应为空";
            break;
        }

        QJsonObject dataObj = parseJsonObject(responseData, "data");
        QJsonArray items = dataObj.value("list").toArray();
        if (items.isEmpty()) {
            qWarning() << "获取节目看点失败: 第" << page << "页数据为空";
            break;
        }

        if (page == 1) {
            const int total = dataObj.value("total").toInt(items.size());
            totalPages = std::max(1, (total + pageSize - 1) / pageSize);
            qInfo() << "节目看点总数:" << total << "总页数:" << totalPages;
        }

        processMonthData(items, QStringLiteral("highlight"), result, resultIndex, true, QStringLiteral("看点"));
        QCoreApplication::processEvents();
        ++page;
    } while (page <= totalPages);

    qInfo() << "节目看点获取完成，共获取" << result.size() << "个视频";
    return result;
}

QMap<int, VideoItem> APIService::getFragmentList(const QString& column_id, const QString& item_id)
{
    qInfo() << "获取片段列表，column_id:" << column_id << "item_id:" << item_id;

    QMap<int, VideoItem> result;
    QUrl url = buildTopicVideoListUrl(column_id, item_id, 1);
    QByteArray responseData = sendNetworkRequest(url);
    if (responseData.isEmpty()) {
        qWarning() << "获取片段列表失败: 响应为空";
        return result;
    }

    QJsonObject rootObj = parseJsonObject(responseData);
    if (rootObj.isEmpty()) {
        qWarning() << "获取片段列表失败: 数据格式不正确";
        return result;
    }

    QJsonArray items = rootObj.value("data").toArray();
    if (items.isEmpty()) {
        qWarning() << "获取片段列表为空";
        return result;
    }

    int resultIndex = 0;
    processTopicVideoData(items, result, resultIndex);
    qInfo() << "片段列表获取完成，共获取" << result.size() << "个视频";
    return result;
}

QString APIService::getRealAlbumId(const QString& item_id)
{
    qInfo() << "获取真实专辑ID，item_id:" << item_id;
    
    QUrl url("https://api.cntv.cn/NewVideoset/getVideoAlbumInfoByVideoId");
    QUrlQuery query;
    query.addQueryItem("id", item_id);
    query.addQueryItem("serviceId", "tvcctv");
    url.setQuery(query);

    QByteArray responseData = sendNetworkRequest(url);
    if (responseData.isEmpty()) {
        qWarning() << "获取真实专辑ID失败: 响应数据为空";
        return "";
    }

    QJsonObject dataObj = parseJsonObject(responseData, "data");
    if (dataObj.isEmpty() || !dataObj.contains("id")) {
        qWarning() << "解析真实专辑ID失败: 数据格式不正确";
        return "";
    }

    QString albumId = dataObj["id"].toString();
    qInfo() << "成功获取真实专辑ID:" << albumId;
    
    return albumId;
}

QString APIService::getRealAlbumId(const QString& item_id, QNetworkAccessManager* networkAccessManager)
{
    qInfo() << "获取真实专辑ID，item_id:" << item_id;

    QUrl url("https://api.cntv.cn/NewVideoset/getVideoAlbumInfoByVideoId");
    QUrlQuery query;
    query.addQueryItem("id", item_id);
    query.addQueryItem("serviceId", "tvcctv");
    url.setQuery(query);

    QByteArray responseData = sendNetworkRequest(networkAccessManager, url);
    if (responseData.isEmpty()) {
        qWarning() << "获取真实专辑ID失败: 响应数据为空";
        return "";
    }

    QJsonObject dataObj = parseJsonObject(responseData, "data");
    if (dataObj.isEmpty() || !dataObj.contains("id")) {
        qWarning() << "解析真实专辑ID失败: 数据格式不正确";
        return "";
    }

    QString albumId = dataObj["id"].toString();
    qInfo() << "成功获取真实专辑ID:" << albumId;

    return albumId;
}

QMap<int, VideoItem> APIService::fetchVideoData(
    const QString& id,
	QStringList dateList,
    FetchType fetch_type)
{
    qInfo() << "获取视频数据 - ID:" << id << "类型:" << (fetch_type == FetchType::Column ? "栏目" : "专辑");
    qInfo() << "日期列表:" << dateList;

    QMap<int, VideoItem> result;
    int result_index = 0;

    constexpr int pageSize = 100;
    QNetworkAccessManager localManager;
    QNetworkAccessManager* manager = callScopedNetworkAccessManager(localManager);

    // 按月循环
    for (const QString& date : dateList) {
        qInfo() << "处理月份:" << date << "格式:yyyyMM";

        int page = 1;
        int totalPages = 1;

        do {
            // 构建API URL
            QUrl url = buildVideoApiUrl(fetch_type, id, date, page, pageSize);
            qInfo() << "请求URL:" << url.toString();

            QByteArray responseData = sendNetworkRequest(manager, url);

            if (responseData.isEmpty()) {
                qWarning() << "月份" << date << "第" << page << "页获取数据失败";
                break;
            }

            QJsonObject dataObj = parseJsonObject(responseData, "data");
            QJsonArray items = dataObj.value("list").toArray();
            if (items.isEmpty()) {
                qWarning() << "月份" << date << "第" << page << "页数据为空";
                break;
            }

            if (page == 1) {
                const int total = dataObj.value("total").toInt(items.size());
                totalPages = std::max(1, (total + pageSize - 1) / pageSize);
                qInfo() << "月份" << date << "总数:" << total << "总页数:" << totalPages;
            }

            qInfo() << "月份" << date << "第" << page << "/" << totalPages << "页获取到" << items.size() << "个项目";

            // 处理当前页数据
            processMonthData(items, date, result, result_index);

            // 处理事件循环
            QCoreApplication::processEvents();
            ++page;
        } while (page <= totalPages);
    }

    qInfo() << "获取视频数据完成，共获取" << result.size() << "个视频";

    return result;
}

QUrl APIService::buildVideoApiUrl(FetchType fetch_type, const QString& id, const QString& date, int page = 1, int page_size = 100)
{
    QUrl url;
    QUrlQuery query;

    if (fetch_type == FetchType::Column) {
        url = QUrl("https://api.cntv.cn/NewVideo/getVideoListByColumn");
        query.addQueryItem("sort", "desc");
        qInfo() << "构建栏目API URL";
    }
    else {
        url = QUrl("https://api.cntv.cn/NewVideo/getVideoListByAlbumIdNew");
        query.addQueryItem("sort", "asc");
        query.addQueryItem("pub", "1");
        qInfo() << "构建专辑API URL";
    }

    query.addQueryItem("id", id);
    query.addQueryItem("n", QString::number(page_size));
    query.addQueryItem("p", QString::number(page));
	query.addQueryItem("d", date);
    query.addQueryItem("mode", "0");
    query.addQueryItem("serviceId", "tvcctv");

    url.setQuery(query);
    qInfo() << "构建的API URL:" << url.toString();
    
    return url;
}

QUrl APIService::buildVcctvProgrammeVideoListUrl(const QString& mid, const QString& chid,
    int page, int pageSize)
{
    QUrl url(QStringLiteral("https://media.app.cctv.com/vapi/video/vplist.do"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("mid"), mid);
    query.addQueryItem(QStringLiteral("chid"), chid);
    query.addQueryItem(QStringLiteral("p"), QString::number(page));
    query.addQueryItem(QStringLiteral("n"), QString::number(pageSize));
    url.setQuery(query);
    return url;
}

QUrl APIService::buildAlbumVideoListUrl(const QString& album_id, int mode, int page, int page_size)
{
    QUrl url("https://api.cntv.cn/NewVideo/getVideoListByAlbumIdNew");
    QUrlQuery query;
    query.addQueryItem("id", album_id);
    query.addQueryItem("serviceId", "tvcctv");
    query.addQueryItem("pub", "1");
    query.addQueryItem("sort", "asc");
    query.addQueryItem("mode", QString::number(mode));
    query.addQueryItem("p", QString::number(page));
    query.addQueryItem("n", QString::number(page_size));
    url.setQuery(query);
    return url;
}

QUrl APIService::buildTopicVideoListUrl(const QString& column_id, const QString& item_id, int type)
{
    QUrl url("https://api.cntv.cn/video/getVideoListByTopicIdInfo");
    QUrlQuery query;
    query.addQueryItem("videoid", item_id);
    query.addQueryItem("topicid", column_id);
    query.addQueryItem("serviceId", "tvcctv");
    query.addQueryItem("type", QString::number(type));
    url.setQuery(query);
    return url;
}

QJsonObject APIService::parseJsonObject(const QByteArray& data, const QString& key)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "JSON解析失败:" << parseError.errorString();
        return QJsonObject();
    }

    QJsonObject rootObj = doc.object();
    QJsonObject result = key.isEmpty()
        ? rootObj
        : (rootObj.contains(key) ? rootObj[key].toObject() : QJsonObject());
    
    if (result.isEmpty()) {
        qDebug() << "JSON对象中未找到键:" << key;
    }
    
    return result;
}

QJsonArray APIService::parseJsonArray(const QByteArray& data, const QString& objectKey, const QString& arrayKey)
{
    QJsonObject dataObj = parseJsonObject(data, objectKey);
    QJsonArray result = dataObj.contains(arrayKey) ? dataObj[arrayKey].toArray() : QJsonArray();
    
    if (result.isEmpty()) {
        qDebug() << "JSON数组中未找到键:" << arrayKey << "在对象键:" << objectKey;
    } else {
        qDebug() << "成功解析JSON数组，大小:" << result.size();
    }
    
    return result;
}

void APIService::processMonthData(
    const QJsonArray& items,
    const QString& month,
    QMap<int, VideoItem>& result,
    int& result_index,
    bool isHighlight,
    const QString& listType)
{
    int processedCount = 0;
    int skippedCount = 0;

    qInfo() << "处理月份" << month << "的数据，共" << items.size() << "个项目";

    for (int i = 0; i < items.size(); ++i) {
        QJsonObject item = items[i].toObject();

        // 验证必要字段
        if (!item.contains("guid") || !item.contains("title")) {
            qWarning() << "月份" << month << " - 跳过无效项目: 缺少必要字段guid或title";
            skippedCount++;
            continue;
        }

        // 创建VideoItem
        VideoItem videoItem;
        videoItem.guid = item["guid"].toString();
        videoItem.time = item["time"].toString();
        videoItem.title = item["title"].toString();
        videoItem.image = item["image"].toString();
        videoItem.brief = item["brief"].toString();
        videoItem.channel = readVideoChannel(item);
        videoItem.length = parseDurationSeconds(item["length"]);
        videoItem.isHighlight = isHighlight;
        videoItem.listType = listType;

        // 添加到结果集
        result[result_index++] = videoItem;
        processedCount++;

        // 调试输出
        if (processedCount % 10 == 0) {
            qDebug() << "月份" << month << " - 已处理" << processedCount << "个视频";
        }
    }

    qInfo() << "月份" << month << "数据处理完成 - 成功处理:" << processedCount
        << "个，跳过:" << skippedCount << "个";
}

void APIService::processTopicVideoData(const QJsonArray& items, QMap<int, VideoItem>& result, int& result_index)
{
    int processedCount = 0;
    int skippedCount = 0;

    for (int i = 0; i < items.size(); ++i) {
        QJsonObject item = items[i].toObject();
        if (!item.contains("guid") || !item.contains("video_title")) {
            skippedCount++;
            continue;
        }

        VideoItem videoItem;
        videoItem.guid = item["guid"].toString();
        videoItem.time = item["video_focus_date"].toString();
        videoItem.title = item["video_title"].toString();
        videoItem.image = item["video_key_frame_url"].toString();
        videoItem.brief = item["sc"].toString();
        videoItem.channel = readVideoChannel(item);
        videoItem.length = parseDurationSeconds(item["length"]);
        videoItem.isHighlight = true;
        videoItem.listType = QStringLiteral("片段");

        result[result_index++] = videoItem;
        ++processedCount;
    }

    qInfo() << "片段数据处理完成 - 成功处理:" << processedCount
        << "个，跳过:" << skippedCount << "个";
}

bool APIService::parseVideoInfo(const QByteArray& data, QString& channel, qint64& length)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonObject video = root.value(QStringLiteral("video")).toObject();
    channel = readVideoChannel(root);
    length = parseDurationSeconds(video.value(QStringLiteral("totalLength")));
    return !channel.isEmpty() || length >= 0;
}

quint64 APIService::nextAsyncBrowseRequestId()
{
    QMutexLocker locker(&m_mutex);
    return ++m_nextAsyncBrowseRequestId;
}

quint64 APIService::startGetPlayColumnInfo(const QString& url)
{
    const quint64 requestId = nextAsyncBrowseRequestId();
    {
        QMutexLocker locker(&m_mutex);
        m_activePlayColumnInfoRequestId = requestId;
    }

    auto publishResult = [this, requestId, url]() {
        const QSharedPointer<ContentParse::ImportResult> result = getPlayColumnInfo(url);
        const bool matchesActiveRequest = [this, requestId]() {
            QMutexLocker locker(&m_mutex);
            return m_activePlayColumnInfoRequestId == requestId;
        }();
        if (!matchesActiveRequest) {
            return;
        }

        if (!result.isNull() && result->isValid()) {
            emit playColumnInfoResolved(requestId, *result);
            return;
        }

        emit playColumnInfoFailed(requestId, QStringLiteral("获取栏目信息失败"));
    };

#ifdef CORE_REGRESSION_TESTS
    if (m_testNetworkAccessManager) {
        QTimer::singleShot(0, this, publishResult);
        return requestId;
    }
#endif

    QThread* workerThread = QThread::create([this, requestId, url]() {
        const QSharedPointer<ContentParse::ImportResult> result = getPlayColumnInfo(url);
        const bool matchesActiveRequest = [this, requestId]() {
            QMutexLocker locker(&m_mutex);
            return m_activePlayColumnInfoRequestId == requestId;
        }();
        if (!matchesActiveRequest) {
            return;
        }

        if (!result.isNull() && result->isValid()) {
            const ContentParse::ImportResult data = *result;
            QMetaObject::invokeMethod(this, [this, requestId, data]() {
                emit playColumnInfoResolved(requestId, data);
            }, Qt::QueuedConnection);
            return;
        }

        QMetaObject::invokeMethod(this, [this, requestId]() {
            emit playColumnInfoFailed(requestId, QStringLiteral("获取栏目信息失败"));
        }, Qt::QueuedConnection);
    });
    workerThread->setObjectName(QStringLiteral("APIServicePlayColumnInfoWorker"));
    connect(workerThread, &QThread::finished, workerThread, &QObject::deleteLater);
    workerThread->start();
    return requestId;
}

quint64 APIService::startGetBrowseVideoList(const ContentParse::ImportResult& result,
    const QString& start_date,
    const QString& end_date,
    bool includeHighlights)
{
    return startGetBrowseVideoList(ContentParse::makeProgrammeRecord(result), start_date, end_date, includeHighlights);
}

quint64 APIService::startGetBrowseVideoList(const ContentParse::ProgrammeRecord& record,
    const QString& start_date,
    const QString& end_date,
    bool includeHighlights)
{
    if (!record.isValid()) {
        return 0;
    }

    const quint64 requestId = nextAsyncBrowseRequestId();
    {
        QMutexLocker locker(&m_mutex);
        m_activeBrowseVideoListRequestId = requestId;
    }

    auto publishResult = [this, requestId, record, start_date, end_date, includeHighlights]() {
        QMap<int, VideoItem> videos = getVideoList(record, start_date, end_date);

        if (includeHighlights) {
            appendUniqueVideos(videos, getHighlightList(record.itemId));
            appendUniqueVideos(videos, getFragmentList(record.columnId, record.itemId));
        }
        QMutexLocker locker(&m_mutex);
        if (m_activeBrowseVideoListRequestId == requestId) {
            QMetaObject::invokeMethod(this, [this, requestId, videos]() {
                emit browseVideoListResolved(requestId, videos);
            }, Qt::QueuedConnection);
        }
    };

#ifdef CORE_REGRESSION_TESTS
    if (m_testNetworkAccessManager) {
        QTimer::singleShot(0, this, publishResult);
        return requestId;
    }
#endif

    QThread* workerThread = QThread::create(publishResult);
    workerThread->setObjectName(QStringLiteral("APIServiceBrowseVideoListWorker"));
    connect(workerThread, &QThread::finished, workerThread, &QObject::deleteLater);
    workerThread->start();
    return requestId;
}

quint64 APIService::startGetBrowseVideoList(const QString& column_id,
    const QString& item_id,
    const QString& start_date,
    const QString& end_date,
    bool includeHighlights)
{
    const quint64 requestId = nextAsyncBrowseRequestId();
    {
        QMutexLocker locker(&m_mutex);
        m_activeBrowseVideoListRequestId = requestId;
    }

    auto publishResult = [this, requestId, column_id, item_id, start_date, end_date, includeHighlights]() {
        QMap<int, VideoItem> videos = getVideoList(column_id, item_id, start_date, end_date);

        if (includeHighlights) {
            appendUniqueVideos(videos, getHighlightList(item_id));
            appendUniqueVideos(videos, getFragmentList(column_id, item_id));
        }

        const bool matchesActiveRequest = [this, requestId]() {
            QMutexLocker locker(&m_mutex);
            return m_activeBrowseVideoListRequestId == requestId;
        }();
        if (!matchesActiveRequest) {
            return;
        }

        emit browseVideoListResolved(requestId, videos);
    };

#ifdef CORE_REGRESSION_TESTS
    if (m_testNetworkAccessManager) {
        QTimer::singleShot(0, this, publishResult);
        return requestId;
    }
#endif

    QThread* workerThread = QThread::create([this, requestId, column_id, item_id, start_date, end_date, includeHighlights]() {
        QMap<int, VideoItem> videos = getVideoList(column_id, item_id, start_date, end_date);

        if (includeHighlights) {
            appendUniqueVideos(videos, getHighlightList(item_id));
            appendUniqueVideos(videos, getFragmentList(column_id, item_id));
        }

        const bool matchesActiveRequest = [this, requestId]() {
            QMutexLocker locker(&m_mutex);
            return m_activeBrowseVideoListRequestId == requestId;
        }();
        if (!matchesActiveRequest) {
            return;
        }

        QMetaObject::invokeMethod(this, [this, requestId, videos]() {
            emit browseVideoListResolved(requestId, videos);
        }, Qt::QueuedConnection);
    });
    workerThread->setObjectName(QStringLiteral("APIServiceBrowseVideoListWorker"));
    connect(workerThread, &QThread::finished, workerThread, &QObject::deleteLater);
    workerThread->start();
    return requestId;
}

quint64 APIService::startGetVideoInfo(const QString& guid)
{
    const quint64 requestId = nextAsyncBrowseRequestId();
    {
        QMutexLocker locker(&m_mutex);
        m_activeVideoInfoRequestId = requestId;
    }

    QUrl url(QStringLiteral("https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("pid"), guid);
    url.setQuery(query);

    QNetworkReply* reply = networkAccessManager()->get(buildNetworkRequest(url));
    connect(reply, &QNetworkReply::errorOccurred, [reply](QNetworkReply::NetworkError error) {
        if (error == QNetworkReply::SslHandshakeFailedError) {
            reply->ignoreSslErrors();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, requestId, guid, reply]() {
        const bool matchesActiveRequest = [this, requestId]() {
            QMutexLocker locker(&m_mutex);
            return m_activeVideoInfoRequestId == requestId;
        }();
        if (!matchesActiveRequest) {
            reply->deleteLater();
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            const QString errorMessage = reply->errorString();
            reply->deleteLater();
            emit videoInfoFailed(requestId, guid, errorMessage);
            return;
        }

        QString channel;
        qint64 length = -1;
        const bool parsed = parseVideoInfo(reply->readAll(), channel, length);
        reply->deleteLater();
        if (!parsed) {
            emit videoInfoFailed(requestId, guid, QStringLiteral("视频详情数据格式无效"));
            return;
        }

        emit videoInfoResolved(requestId, guid, channel, length);
    });
    return requestId;
}

