#include "../head/apiservice.h"
#include <QCoreApplication>
#include <algorithm>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QImage>
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
#include <utility>

// 静态成员初始化
QPointer<APIService> APIService::m_instance = nullptr;
QMutex APIService::m_instanceMutex;

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

QNetworkRequest APIService::buildNetworkRequest(const QUrl& url, const QHash<QString, QString>& headers) const
{
    QNetworkRequest request(url);

    QSslConfiguration sslConfig = request.sslConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
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
    qInfo() << "发送网络请求:" << url.toString();

    QNetworkAccessManager localManager;
    QNetworkAccessManager* manager = &localManager;
#ifdef CORE_REGRESSION_TESTS
    if (m_testNetworkAccessManager) {
        manager = m_testNetworkAccessManager;
    }
#endif

    QNetworkRequest request = buildNetworkRequest(url, headers);

    QNetworkReply* reply = manager->get(request);
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
#endif

QSharedPointer<QStringList> APIService::getPlayColumnInfo(const QString& url) {
    qInfo() << "获取播放栏目信息，URL:" << url;
    
    QByteArray responseData = sendNetworkRequest(QUrl(url));
    if (responseData.isEmpty()) {
        qWarning() << "获取播放栏目信息失败: 响应数据为空";
        return nullptr;
    }

    QString html = QString::fromUtf8(responseData);
    auto results = QSharedPointer<QStringList>::create();

    // 预编译正则表达式
    static const QRegularExpression regexPatterns[] = {
        QRegularExpression(R"(var commentTitle\s*=\s*["'](.*?)["'];)"),
        QRegularExpression(R"(var itemid1\s*=\s*["'](.*?)["'];)"),
        QRegularExpression(R"(var column_id\s*=\s*["'](.*?)["'];)"),
        QRegularExpression(R"(var guid\s*=\s*["'](.*?)["'];)")
    };

    // 安全匹配函数
    auto safeMatch = [](const QRegularExpression& regex, const QString& text) -> QString {
        auto match = regex.match(text);
        return match.hasMatch() ? match.captured(1) : QString();
        };

    // 提取标题、itemId、columnId
    QString title = safeMatch(regexPatterns[0], html).split(" ").value(0);
    QString itemId = safeMatch(regexPatterns[1], html);
    QString columnId = safeMatch(regexPatterns[2], html);
    QString guid = safeMatch(regexPatterns[3], html);

    if (title.isEmpty() || itemId.isEmpty() || columnId.isEmpty()) {
        QRegularExpression lmUrlRegex(R"(tv\.cctv\.com/lm/([^/?#]+))");
        auto lmUrlMatch = lmUrlRegex.match(url);
        if (lmUrlMatch.hasMatch()) {
            qInfo() << "尝试从栏目首页提取栏目信息";

            QString lmTitle = safeMatch(QRegularExpression(R"(<meta\s+property=["']og:title["']\s+content=["'](.*?)["'])", QRegularExpression::CaseInsensitiveOption), html).trimmed();
            if (lmTitle.isEmpty()) {
                lmTitle = safeMatch(QRegularExpression(R"(<title>\s*(.*?)\s*(?:_CCTV|</title>))", QRegularExpression::CaseInsensitiveOption), html).trimmed();
            }

            QString lmItemId = safeMatch(QRegularExpression(R"(play\(\s*["']([0-9a-fA-F]{32})["'])"), html).trimmed();

            QString videosetUrl = QString("https://tv.cctv.com/lm/%1/videoset").arg(lmUrlMatch.captured(1));
            QByteArray videosetData = sendNetworkRequest(QUrl(videosetUrl));
            QString lmColumnId;
            if (!videosetData.isEmpty()) {
                lmColumnId = safeMatch(QRegularExpression(R"(var\s+lmtopId\s*=\s*["'](TOPC\d+)["'];)"), QString::fromUtf8(videosetData)).trimmed();
            }

            if (!lmTitle.isEmpty() && !lmColumnId.isEmpty()) {
                title = lmTitle;
                itemId = lmItemId.isEmpty() ? lmColumnId : lmItemId;
                columnId = lmColumnId;
            }
        }
    }

    if (columnId.isEmpty() && !guid.isEmpty()) {
        qInfo() << "未获取到columnId，使用guid作为CCTV-4K兜底:" << guid;
        title = title.isEmpty() ? QStringLiteral("CCTV-4K") : title;
        itemId = itemId.isEmpty() ? guid : itemId;
        columnId = guid;
    }

    // 验证数据完整性
    if (title.isEmpty() || itemId.isEmpty() || columnId.isEmpty()) {
        qWarning() << "从HTML提取必要数据失败";
        return nullptr;
    }

    qInfo() << "成功提取播放栏目信息 - 标题:" << title << "itemId:" << itemId << "columnId:" << columnId;

    results->append(title);
    results->append(itemId);
    results->append(columnId);

    return results;
}

QMap<int, VideoItem> APIService::getVideoList(
    const QString& column_id,
    const QString& item_id,
    const QString& start_date,
    const QString& end_date)
{
    qInfo() << "获取视频列表 - column_id:" << column_id << "item_id:" << item_id
             << "start_index:" << start_date << "end_index:" << end_date;
    
    // 参数校验
	QDate dateBegin = QDateTime::fromString(start_date, "yyyyMM").date();
	QDate dateEnd = QDateTime::fromString(end_date, "yyyyMM").date();

    if (dateBegin < dateEnd) {
		QDate tmp = dateBegin;
		dateBegin = dateEnd;
		dateEnd = tmp;
    }

    // 生成日期列表
	QStringList dateList;
    for (QDate date = dateBegin; date >= dateEnd; date = date.addMonths(-1)) {
		dateList.append(date.toString("yyyyMM"));
    }
	qInfo() << "生成的日期列表:" << dateList;

    // 先尝试栏目方式获取
    QMap<int, VideoItem> result = fetchVideoData(column_id, dateList, FetchType::Column);

    if (!result.isEmpty()) {
        qInfo() << "通过栏目方式获取到" << result.size() << "个视频";
        return result;
    }

    qInfo() << "栏目方式获取失败，尝试专辑方式";
    
    // 尝试专辑方式获取
    QString real_album_id = getRealAlbumId(item_id);
    if (!real_album_id.isEmpty()) {
        qInfo() << "获取到真实专辑ID:" << real_album_id;
        result = fetchVideoData(real_album_id, dateList, FetchType::Album);
        if (!result.isEmpty()) {
            qInfo() << "通过专辑方式获取到" << result.size() << "个视频";
        }
    } else {
        qWarning() << "无法获取真实专辑ID";
    }

    if (result.isEmpty()) {
        QUrl videoInfoUrl("https://zy.api.cntv.cn/video/videoinfoByGuid");
        QUrlQuery query;
        query.addQueryItem("serviceId", "cctv4k");
        query.addQueryItem("guid", column_id);
        videoInfoUrl.setQuery(query);

        qInfo() << "尝试通过CCTV-4K接口获取单视频信息:" << videoInfoUrl.toString();
        QByteArray responseData = sendNetworkRequest(videoInfoUrl);
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            QJsonObject videoObj = doc.object();
            QString title = videoObj["title"].toString();
            if (!title.isEmpty()) {
                VideoItem videoItem;
                videoItem.guid = videoObj["vid"].toString();
                videoItem.title = title;
                videoItem.brief = videoObj["brief"].toString();
                videoItem.image = videoObj["img"].toString();
                videoItem.time = videoObj["time"].toString();
                if (videoItem.guid.isEmpty()) {
                    videoItem.guid = column_id;
                }
                result.insert(0, videoItem);
                qInfo() << "通过CCTV-4K接口获取到视频信息 - 标题:" << videoItem.title << "GUID:" << videoItem.guid;
            }
        }
    }

    if (result.isEmpty()) {
        qWarning() << "获取视频列表失败: 所有方式都未获取到数据";
    }

    return result;
}

QMap<int, VideoItem> APIService::getHighlightList(const QString& item_id)
{
    qInfo() << "获取节目看点列表，item_id:" << item_id;

    QMap<int, VideoItem> result;
    QString real_album_id = getRealAlbumId(item_id);
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
        QByteArray responseData = sendNetworkRequest(url);
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

    // 按月循环
    for (const QString& date : dateList) {
        qInfo() << "处理月份:" << date << "格式:yyyyMM";

        int page = 1;
        int totalPages = 1;

        do {
            // 构建API URL
            QUrl url = buildVideoApiUrl(fetch_type, id, date, page, pageSize);
            qInfo() << "请求URL:" << url.toString();

            QByteArray responseData = sendNetworkRequest(url);

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
        videoItem.isHighlight = true;
        videoItem.listType = QStringLiteral("片段");

        result[result_index++] = videoItem;
        ++processedCount;
    }

    qInfo() << "片段数据处理完成 - 成功处理:" << processedCount
        << "个，跳过:" << skippedCount << "个";
}

QImage APIService::getImage(const QString& url)
{
    qInfo() << "获取图片，URL:" << url;
    
    QByteArray imageData = sendNetworkRequest(QUrl(url));
    if (imageData.isEmpty()) {
        qWarning() << "获取图片失败: 响应数据为空";
        return QImage();
    }

    qInfo() << "图片数据大小:" << imageData.size() << "字节";

    QImage image;
    if (!image.loadFromData(imageData)) {
        qWarning() << "从数据加载图片失败，URL:" << url;
        return QImage();
    }

    qInfo() << "图片加载成功，尺寸:" << image.width() << "x" << image.height() << "格式:" << image.format();

    // 转换为标准格式以提高性能
    if (image.format() != QImage::Format_ARGB32 &&
        image.format() != QImage::Format_RGB32) {
        image = image.convertToFormat(QImage::Format_ARGB32);
        qInfo() << "图片已转换为标准格式";
    }

    return image;
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
        const QSharedPointer<QStringList> result = getPlayColumnInfo(url);
        const bool matchesActiveRequest = [this, requestId]() {
            QMutexLocker locker(&m_mutex);
            return m_activePlayColumnInfoRequestId == requestId;
        }();
        if (!matchesActiveRequest) {
            return;
        }

        if (!result.isNull() && result->size() == 3 && !result->at(0).isEmpty()) {
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
        const QSharedPointer<QStringList> result = getPlayColumnInfo(url);
        const bool matchesActiveRequest = [this, requestId]() {
            QMutexLocker locker(&m_mutex);
            return m_activePlayColumnInfoRequestId == requestId;
        }();
        if (!matchesActiveRequest) {
            return;
        }

        if (!result.isNull() && result->size() == 3 && !result->at(0).isEmpty()) {
            const QStringList data = *result;
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

        auto appendExtraVideos = [&videos](const QMap<int, VideoItem>& extras) {
            int nextIndex = videos.isEmpty() ? 0 : (videos.lastKey() + 1);
            for (const VideoItem& item : extras) {
                bool alreadyListed = false;
                for (const VideoItem& existing : std::as_const(videos)) {
                    if (!item.guid.isEmpty() && item.guid == existing.guid) {
                        alreadyListed = true;
                        break;
                    }
                }
                if (alreadyListed) {
                    continue;
                }
                videos.insert(nextIndex++, item);
            }
        };

        if (includeHighlights) {
            appendExtraVideos(getHighlightList(item_id));
            appendExtraVideos(getFragmentList(column_id, item_id));
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

        auto appendExtraVideos = [&videos](const QMap<int, VideoItem>& extras) {
            int nextIndex = videos.isEmpty() ? 0 : (videos.lastKey() + 1);
            for (const VideoItem& item : extras) {
                bool alreadyListed = false;
                for (const VideoItem& existing : std::as_const(videos)) {
                    if (!item.guid.isEmpty() && item.guid == existing.guid) {
                        alreadyListed = true;
                        break;
                    }
                }
                if (alreadyListed) {
                    continue;
                }
                videos.insert(nextIndex++, item);
            }
        };

        if (includeHighlights) {
            appendExtraVideos(getHighlightList(item_id));
            appendExtraVideos(getFragmentList(column_id, item_id));
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

quint64 APIService::startGetImage(const QString& url)
{
    const quint64 requestId = nextAsyncBrowseRequestId();
    {
        QMutexLocker locker(&m_mutex);
        m_activeImageRequestId = requestId;
    }

    auto publishResult = [this, requestId, url]() {
        const QImage image = getImage(url);
        const bool matchesActiveRequest = [this, requestId]() {
            QMutexLocker locker(&m_mutex);
            return m_activeImageRequestId == requestId;
        }();
        if (!matchesActiveRequest) {
            return;
        }

        emit imageResolved(requestId, url, image);
    };

#ifdef CORE_REGRESSION_TESTS
    if (m_testNetworkAccessManager) {
        QTimer::singleShot(0, this, publishResult);
        return requestId;
    }
#endif

    QThread* workerThread = QThread::create([this, requestId, url]() {
        const QImage image = getImage(url);
        const bool matchesActiveRequest = [this, requestId]() {
            QMutexLocker locker(&m_mutex);
            return m_activeImageRequestId == requestId;
        }();
        if (!matchesActiveRequest) {
            return;
        }

        QMetaObject::invokeMethod(this, [this, requestId, url, image]() {
            emit imageResolved(requestId, url, image);
        }, Qt::QueuedConnection);
    });
    workerThread->setObjectName(QStringLiteral("APIServiceImageWorker"));
    connect(workerThread, &QThread::finished, workerThread, &QObject::deleteLater);
    workerThread->start();
    return requestId;
}

void APIService::startGetEncryptM3U8Urls(const QString& GUID, const QString& quality)
{
    qInfo() << "异步获取加密M3U8 URL，GUID:" << GUID << "质量:" << quality;

    if (m_activeM3u8ResolveId != 0) {
        cancelGetEncryptM3U8Urls();
    }

    m_lastM3U8ResultWas4K = false;
    m_pendingGuid = GUID;
    m_pendingQuality = quality;
    m_pendingMasterPlaylistUrl.clear();
    m_m3u8ResolveStage = M3u8ResolveStage::FetchInfo;
    ++m_activeM3u8ResolveId;

    QUrl infoUrl("https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do");
    QUrlQuery infoQuery;
    infoQuery.addQueryItem("pid", GUID);
    infoUrl.setQuery(infoQuery);
    startM3u8NetworkRequest(m_activeM3u8ResolveId, infoUrl);
}

void APIService::cancelGetEncryptM3U8Urls()
{
    if (m_activeM3u8ResolveId == 0) {
        return;
    }

    qInfo() << "取消异步M3U8解析，GUID:" << m_pendingGuid;

    QPointer<QNetworkReply> reply = m_pendingM3u8Reply;
    m_pendingM3u8Reply = nullptr;
    m_activeM3u8ResolveId = 0;
    m_m3u8ResolveStage = M3u8ResolveStage::None;
    m_pendingGuid.clear();
    m_pendingQuality.clear();
    m_pendingMasterPlaylistUrl.clear();
    m_lastM3U8ResultWas4K = false;

    emit encryptM3U8UrlsCancelled();

    if (reply) {
        reply->abort();
    }
}

#ifdef CORE_REGRESSION_TESTS
QStringList APIService::getEncryptM3U8Urls(const QString& GUID, const QString& quality)
{
    QStringList resolvedUrls;
    bool done = false;

    const QMetaObject::Connection successConnection = connect(this,
        &APIService::encryptM3U8UrlsResolved,
        this,
        [&](const QStringList& urls, bool) {
            resolvedUrls = urls;
            done = true;
        },
        Qt::DirectConnection);
    const QMetaObject::Connection failedConnection = connect(this,
        &APIService::encryptM3U8UrlsFailed,
        this,
        [&](const QString&) {
            done = true;
        },
        Qt::DirectConnection);
    const QMetaObject::Connection cancelledConnection = connect(this,
        &APIService::encryptM3U8UrlsCancelled,
        this,
        [&]() {
            done = true;
        },
        Qt::DirectConnection);

    startGetEncryptM3U8Urls(GUID, quality);

    while (!done) {
        QCoreApplication::processEvents();
    }

    disconnect(successConnection);
    disconnect(failedConnection);
    disconnect(cancelledConnection);
    return resolvedUrls;
}
#endif

QHash<QString, QString> APIService::parseM3U8QualityUrls(const QByteArray& m3u8Data, const QString& baseUrl)
{
    qDebug() << "解析M3U8质量URL";
    
    QStringList m3u8Lines = QString::fromUtf8(m3u8Data).split("\n");
    QHash<QString, QualityInfo> qualityMap = {
        {"5", {4000000, "3840x2160"}},
        {"4", {460800, "480x270"}},
        {"3", {870400, "640x360"}},
        {"2", {1228800, "1280x720"}},
        {"1", {2048000, "1280x720"}}
    };

    QHash<QString, QString> qualityUrls;
    QString currentQuality;

    for (const QString& line : m3u8Lines) {
        QString trimmedLine = line.trimmed();

        if (trimmedLine.startsWith("#EXT-X-STREAM-INF")) {
            QRegularExpression re("BANDWIDTH=(\\d+)");
            QRegularExpressionMatch match = re.match(trimmedLine);
            if (match.hasMatch()) {
                int bandwidth = match.captured(1).toInt();
                currentQuality = findQualityByBandwidth(qualityMap, bandwidth);
                qDebug() << "发现质量流 - 带宽:" << bandwidth << "质量:" << currentQuality;
            }
        }
        else if (!trimmedLine.startsWith("#") && !currentQuality.isEmpty() && !trimmedLine.isEmpty()) {
            qualityUrls[currentQuality] = trimmedLine;
            qDebug() << "质量" << currentQuality << "的URL:" << trimmedLine;
            currentQuality.clear();
        }
    }

    qDebug() << "解析完成，找到" << qualityUrls.size() << "个质量选项";
    
    return qualityUrls;
}

QString APIService::findQualityByBandwidth(const QHash<QString, QualityInfo>& qualityMap, int bandwidth)
{
    for (auto it = qualityMap.begin(); it != qualityMap.end(); ++it) {
        if (it.value().bandwidth == bandwidth) {
            return it.key();
        }
    }
    if (bandwidth >= 4000000) {
        return QStringLiteral("5");
    }
    return QString();
}

QString APIService::selectQuality(const QString& requestedQuality, const QHash<QString, QString>& availableQualities)
{
    qDebug() << "选择质量，请求质量:" << requestedQuality;
    
    if (requestedQuality == "0") {
        // 自动选择最高质量
        if (availableQualities.isEmpty()) {
            qWarning() << "自动选择质量失败: 无可用的质量选项";
            return QString();
        }

        static const QHash<QString, int> qualityBandwidths = {
            {"5", 4000000},
            {"1", 2048000},
            {"2", 1228800},
            {"3", 870400},
            {"4", 460800}
        };

        QString selected;
        int maxBandwidth = -1;

        for (auto it = availableQualities.begin(); it != availableQualities.end(); ++it) {
            const int bandwidth = qualityBandwidths.value(it.key(), -1);
            if (bandwidth > maxBandwidth) {
                maxBandwidth = bandwidth;
                selected = it.key();
            }
        }

        if (selected.isEmpty()) {
            qWarning() << "自动选择质量失败: 未匹配到已知质量档位，使用首个可用项";
            selected = availableQualities.constBegin().key();
        }

        qDebug() << "自动选择最高质量:" << selected;
        return selected;
    }

    if (availableQualities.contains(requestedQuality)) {
        qDebug() << "使用请求的质量:" << requestedQuality;
        return requestedQuality;
    }

    qWarning() << "请求的质量不可用:" << requestedQuality
        << "可用的质量:" << availableQualities.keys().join(", ");
    return QString();
}

bool APIService::lastM3U8ResultWas4K() const
{
    return m_lastM3U8ResultWas4K;
}

void APIService::startM3u8NetworkRequest(quint64 requestId, const QUrl& url)
{
    if (requestId != m_activeM3u8ResolveId || requestId == 0) {
        return;
    }

    qInfo() << "异步M3U8请求:" << url.toString() << "阶段:" << static_cast<int>(m_m3u8ResolveStage);

    QNetworkReply* reply = networkAccessManager()->get(buildNetworkRequest(url));
    m_pendingM3u8Reply = reply;

    QObject::connect(reply, &QNetworkReply::errorOccurred, this,
        [reply](QNetworkReply::NetworkError error) {
            if (error == QNetworkReply::SslHandshakeFailedError) {
                qWarning() << "SSL握手失败，尝试忽略错误:" << reply->errorString();
                reply->ignoreSslErrors();
            }
        });

    QObject::connect(reply, &QNetworkReply::finished, this,
        [this, requestId]() {
            handleM3u8ReplyFinished(requestId);
        });
}

void APIService::handleM3u8ReplyFinished(quint64 requestId)
{
    if (requestId != m_activeM3u8ResolveId || requestId == 0 || !m_pendingM3u8Reply) {
        return;
    }

    QNetworkReply* reply = m_pendingM3u8Reply;
    m_pendingM3u8Reply = nullptr;
    const M3u8ResolveStage stage = m_m3u8ResolveStage;

    if (reply->error() != QNetworkReply::NoError) {
        const QString errorMessage = reply->error() == QNetworkReply::OperationCanceledError
            ? QStringLiteral("M3U8解析已取消")
            : QStringLiteral("网络请求失败: %1").arg(reply->errorString());
        reply->deleteLater();
        finishM3u8ResolveFailure(requestId, errorMessage);
        return;
    }

    const QByteArray responseData = reply->readAll();
    const QString requestUrl = reply->url().toString();
    reply->deleteLater();

    if (responseData.isEmpty()) {
        finishM3u8ResolveFailure(requestId, QStringLiteral("网络响应为空: %1").arg(requestUrl));
        return;
    }

    if (stage == M3u8ResolveStage::FetchInfo) {
        QJsonParseError infoParseError;
        QJsonDocument infoDoc = QJsonDocument::fromJson(responseData, &infoParseError);
        if (infoParseError.error == QJsonParseError::NoError && infoDoc.isObject()) {
            const QJsonObject rootObj = infoDoc.object();
            const QString playChannel = rootObj["play_channel"].toString();
            if (playChannel.contains(QStringLiteral("CCTV-4K"), Qt::CaseInsensitive)) {
                QString hlsUrl = rootObj["hls_url"].toString();
                if (hlsUrl.isEmpty()) {
                    finishM3u8ResolveFailure(requestId, QStringLiteral("CCTV-4K视频hls_url为空"));
                    return;
                }

                hlsUrl.replace(QStringLiteral("main"), QStringLiteral("4000"));
                m_m3u8ResolveStage = M3u8ResolveStage::Fetch4KPlaylist;
                m_pendingMasterPlaylistUrl = hlsUrl;
                startM3u8NetworkRequest(requestId, QUrl(hlsUrl));
                return;
            }
        }

        const QJsonObject manifestObj = parseJsonObject(responseData, "manifest");
        QString hlsH5eUrl = manifestObj["hls_enc2_url"].toString();
        if (hlsH5eUrl.isEmpty()) {
            finishM3u8ResolveFailure(requestId, QStringLiteral("无法获取hls_enc2_url"));
            return;
        }

        m_pendingMasterPlaylistUrl = normalizeEncryptedM3u8Url(hlsH5eUrl);
        m_m3u8ResolveStage = M3u8ResolveStage::FetchMasterPlaylist;
        startM3u8NetworkRequest(requestId, QUrl(m_pendingMasterPlaylistUrl));
        return;
    }

    if (stage == M3u8ResolveStage::Fetch4KPlaylist) {
        QStringList tsList = buildTsUrlsFromPlaylistData(responseData, requestUrl);
        if (tsList.isEmpty()) {
            finishM3u8ResolveFailure(requestId, QStringLiteral("未解析到CCTV-4K TS切片"));
            return;
        }

        finishM3u8ResolveSuccess(requestId, tsList, true);
        return;
    }

    if (stage == M3u8ResolveStage::FetchMasterPlaylist) {
        QHash<QString, QString> qualityUrls = parseM3U8QualityUrls(responseData, m_pendingMasterPlaylistUrl);
        if (qualityUrls.isEmpty()) {
            finishM3u8ResolveFailure(requestId, QStringLiteral("解析M3U8质量信息失败"));
            return;
        }

        QString selectedQuality = selectQuality(m_pendingQuality, qualityUrls);
        if (selectedQuality.isEmpty()) {
            finishM3u8ResolveFailure(requestId, QStringLiteral("选择质量失败"));
            return;
        }

        QString m3u8Host = QUrl(m_pendingMasterPlaylistUrl).host();
        QString fullM3u8Url = "https://" + m3u8Host + qualityUrls[selectedQuality];
        m_m3u8ResolveStage = M3u8ResolveStage::FetchVariantPlaylist;
        startM3u8NetworkRequest(requestId, QUrl(fullM3u8Url));
        return;
    }

    if (stage == M3u8ResolveStage::FetchVariantPlaylist) {
        QStringList tsList = buildTsUrlsFromPlaylistData(responseData, requestUrl);
        if (tsList.isEmpty()) {
            finishM3u8ResolveFailure(requestId, QStringLiteral("未解析到TS切片"));
            return;
        }

        finishM3u8ResolveSuccess(requestId, tsList, false);
        return;
    }

    finishM3u8ResolveFailure(requestId, QStringLiteral("未知的M3U8解析阶段"));
}

void APIService::finishM3u8ResolveSuccess(quint64 requestId, const QStringList& urls, bool is4K)
{
    if (requestId != m_activeM3u8ResolveId || requestId == 0) {
        return;
    }

    m_lastM3U8ResultWas4K = is4K;
    m_activeM3u8ResolveId = 0;
    m_m3u8ResolveStage = M3u8ResolveStage::None;
    m_pendingGuid.clear();
    m_pendingQuality.clear();
    m_pendingMasterPlaylistUrl.clear();

    emit encryptM3U8UrlsResolved(urls, is4K);
}

void APIService::finishM3u8ResolveFailure(quint64 requestId, const QString& errorMessage)
{
    if (requestId != m_activeM3u8ResolveId || requestId == 0) {
        return;
    }

    m_lastM3U8ResultWas4K = false;
    m_activeM3u8ResolveId = 0;
    m_m3u8ResolveStage = M3u8ResolveStage::None;
    m_pendingGuid.clear();
    m_pendingQuality.clear();
    m_pendingMasterPlaylistUrl.clear();

    qWarning() << errorMessage;
    emit encryptM3U8UrlsFailed(errorMessage);
}

QString APIService::normalizeEncryptedM3u8Url(QString hlsH5eUrl) const
{
    QRegularExpression re("https://[^/]+/asp/enc2/");
    QRegularExpressionMatch match = re.match(hlsH5eUrl);

    if (match.hasMatch()) {
        hlsH5eUrl.replace(match.captured(0), "https://drm.cntv.vod.dnsv1.com/asp/enc2/");
    } else {
        qWarning() << "无法替换CDN，使用默认CDN";
    }

    return hlsH5eUrl;
}

QStringList APIService::getTsFileList(const QString& qualityPath, const QString& baseUrl)
{
    qDebug() << "获取TS文件列表，质量路径:" << qualityPath;
    
    QString m3u8Host = QUrl(baseUrl).host();
    QString fullM3u8Url = "https://" + m3u8Host + qualityPath;
    qDebug() << "完整M3U8 URL:" << fullM3u8Url;

    QByteArray videoM3u8Data = sendNetworkRequest(QUrl(fullM3u8Url));
    if (videoM3u8Data.isEmpty()) {
        qWarning() << "获取视频M3U8文件失败: 响应数据为空";
        return QStringList();
    }

    return buildTsUrlsFromPlaylistData(videoM3u8Data, fullM3u8Url);
}

QStringList APIService::buildTsUrlsFromPlaylistData(const QByteArray& videoM3u8Data, const QString& fullM3u8Url)
{
    qDebug() << "视频M3U8文件大小:" << videoM3u8Data.size() << "字节";

    QString normalizedData = QString::fromUtf8(videoM3u8Data).replace("\r\n", "\n").replace("\r", "\n");
    QStringList videoLines = normalizedData.split("\n");
    QStringList tsList;
    QUrl baseUrl(fullM3u8Url);
    baseUrl.setQuery(QString());
    QString basePath = baseUrl.path();
    basePath = basePath.left(basePath.lastIndexOf("/") + 1);
    baseUrl.setPath(basePath);

    for (const QString& line : videoLines) {
        QString trimmedLine = line.trimmed();
        if (trimmedLine.isEmpty() || trimmedLine.startsWith("#")) {
            continue;
        }

        QUrl segmentUrl(trimmedLine);
        if (segmentUrl.scheme().isEmpty() && trimmedLine.startsWith("//")) {
            segmentUrl = QUrl(QUrl(fullM3u8Url).scheme() + ":" + trimmedLine);
        } else if (segmentUrl.isRelative()) {
            segmentUrl = baseUrl.resolved(segmentUrl);
        }

        if (segmentUrl.path().endsWith(".ts", Qt::CaseInsensitive)) {
            tsList << segmentUrl.toString();
        }
    }

    qDebug() << "解析完成，找到" << tsList.size() << "个TS文件";
    
    return tsList;
}

