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
#include <QEventLoop>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QMutexLocker>

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

// 通用的网络请求函数
QByteArray APIService::sendNetworkRequest(const QUrl& url, const QHash<QString, QString>& headers)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(url);

    // 设置默认User-Agent
    request.setHeader(QNetworkRequest::UserAgentHeader,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");

    // 添加自定义头部
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Network request failed:" << reply->errorString() << "URL:" << url.toString();
        reply->deleteLater();
        return QByteArray();
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    if (responseData.isEmpty()) {
        qWarning() << "Empty response data from URL:" << url.toString();
    }

    return responseData;
}

QSharedPointer<QStringList> APIService::getPlayColumnInfo(const QString& url) {
    QByteArray responseData = sendNetworkRequest(QUrl(url));
    if (responseData.isEmpty()) {
        return nullptr;
    }

    QString html = QString::fromUtf8(responseData);
    auto results = QSharedPointer<QStringList>::create();

    // 预编译正则表达式
    static const QRegularExpression regexPatterns[] = {
        QRegularExpression(R"(var commentTitle\s*=\s*["'](.*?)["'];)"),
        QRegularExpression(R"(var itemid1\s*=\s*["'](.*?)["'];)"),
        QRegularExpression(R"(var column_id\s*=\s*["'](.*?)["'];)")
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

    // 验证数据完整性
    if (title.isEmpty() || itemId.isEmpty() || columnId.isEmpty()) {
        qWarning() << "Failed to extract required data from HTML";
        return nullptr;
    }

    results->append(title);
    results->append(itemId);
    results->append(columnId);

    return results;
}

QMap<int, VideoItem> APIService::getVideoList(
    const QString& column_id,
    const QString& item_id,
    int start_index,
    int end_index)
{
    // 参数校验
    if (start_index < 0 || end_index < 0) {
        return {};
    }

    if (start_index > end_index) {
        std::swap(start_index, end_index);
    }

    // 先尝试栏目方式获取
    QMap<int, VideoItem> result = fetchVideoData(column_id, start_index, end_index, FetchType::Column);

    if (!result.isEmpty()) {
        return result;
    }

    // 尝试专辑方式获取
    QString real_album_id = getRealAlbumId(item_id);
    if (!real_album_id.isEmpty()) {
        result = fetchVideoData(real_album_id, start_index, end_index, FetchType::Album);
    }

    return result;
}

QString APIService::getRealAlbumId(const QString& item_id)
{
    QUrl url("https://api.cntv.cn/NewVideoset/getVideoAlbumInfoByVideoId");
    QUrlQuery query;
    query.addQueryItem("id", item_id);
    query.addQueryItem("serviceId", "tvcctv");
    url.setQuery(query);

    QByteArray responseData = sendNetworkRequest(url);
    if (responseData.isEmpty()) {
        return "";
    }

    QJsonObject dataObj = parseJsonObject(responseData, "data");
    if (dataObj.isEmpty() || !dataObj.contains("id")) {
        return "";
    }

    return dataObj["id"].toString();
}

QMap<int, VideoItem> APIService::fetchVideoData(
    const QString& id,
    int start_index,
    int end_index,
    FetchType fetch_type)
{
    constexpr int PAGE_SIZE = 100;
    const int start_page = start_index / PAGE_SIZE + 1;
    const int end_page = end_index / PAGE_SIZE + 1;

    QMap<int, VideoItem> result;
    int result_index = 0;

    for (int page = start_page; page <= end_page; ++page) {
        const int page_start_index = (page - 1) * PAGE_SIZE;

        // 构建API URL
        QUrl url = buildVideoApiUrl(fetch_type, id, page, PAGE_SIZE);
        QByteArray responseData = sendNetworkRequest(url);

        if (responseData.isEmpty()) {
            continue;
        }

        QJsonArray items = parseJsonArray(responseData, "data", "list");
        if (items.isEmpty()) {
            continue;
        }

        // 处理当前页数据
        processPageData(items, page_start_index, start_index, end_index, result, result_index);

        // 处理事件循环
        QCoreApplication::processEvents();
    }

    return result;
}

QUrl APIService::buildVideoApiUrl(FetchType fetch_type, const QString& id, int page, int page_size)
{
    QUrl url;
    QUrlQuery query;

    if (fetch_type == FetchType::Column) {
        url = QUrl("https://api.cntv.cn/NewVideo/getVideoListByColumn");
        query.addQueryItem("sort", "desc");
    }
    else {
        url = QUrl("https://api.cntv.cn/NewVideo/getVideoListByAlbumIdNew");
        query.addQueryItem("sort", "asc");
        query.addQueryItem("pub", "1");
    }

    query.addQueryItem("id", id);
    query.addQueryItem("n", QString::number(page_size));
    query.addQueryItem("p", QString::number(page));
    query.addQueryItem("mode", "0");
    query.addQueryItem("serviceId", "tvcctv");

    url.setQuery(query);
    return url;
}

QJsonObject APIService::parseJsonObject(const QByteArray& data, const QString& key)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return QJsonObject();
    }

    QJsonObject rootObj = doc.object();
    return rootObj.contains(key) ? rootObj[key].toObject() : QJsonObject();
}

QJsonArray APIService::parseJsonArray(const QByteArray& data, const QString& objectKey, const QString& arrayKey)
{
    QJsonObject dataObj = parseJsonObject(data, objectKey);
    return dataObj.contains(arrayKey) ? dataObj[arrayKey].toArray() : QJsonArray();
}

void APIService::processPageData(
    const QJsonArray& items,
    int page_start_index,
    int start_index,
    int end_index,
    QMap<int, VideoItem>& result,
    int& result_index)
{
    for (int i = 0; i < items.size(); ++i) {
        const int current_global_index = page_start_index + i;

        // 检查索引范围
        if (current_global_index < start_index || current_global_index > end_index) {
            continue;
        }

        QJsonObject item = items[i].toObject();

        // 验证必要字段
        if (!item.contains("guid") || !item.contains("title")) {
            continue;
        }

        // 创建VideoItem
        result.insert(result_index, VideoItem{
            item["guid"].toString(),
            item["time"].toString(),
            item["title"].toString(),
            item["image"].toString(),
            item["brief"].toString()
            });

        ++result_index;
    }
}

QImage APIService::getImage(const QString& url)
{
    QByteArray imageData = sendNetworkRequest(QUrl(url));
    if (imageData.isEmpty()) {
        return QImage();
    }

    QImage image;
    if (!image.loadFromData(imageData)) {
        qWarning() << "Failed to load image from data, URL:" << url;
        return QImage();
    }

    // 转换为标准格式以提高性能
    if (image.format() != QImage::Format_ARGB32 &&
        image.format() != QImage::Format_RGB32) {
        image = image.convertToFormat(QImage::Format_ARGB32);
    }

    return image;
}

QStringList APIService::getEncryptM3U8Urls(const QString& GUID, const QString& quality)
{
    // 获取视频信息
    QUrl infoUrl("https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do");
    QUrlQuery infoQuery;
    infoQuery.addQueryItem("pid", GUID);
    infoUrl.setQuery(infoQuery);

    QByteArray infoData = sendNetworkRequest(infoUrl);
    if (infoData.isEmpty()) {
        return QStringList();
    }

    QJsonObject manifestObj = parseJsonObject(infoData, "manifest");
    QString hlsH5eUrl = manifestObj["hls_enc2_url"].toString();

    if (hlsH5eUrl.isEmpty()) {
        qWarning() << "Failed to get hls_enc2_url";
        return QStringList();
    }

    // 获取主M3U8文件
    QByteArray m3u8Data = sendNetworkRequest(QUrl(hlsH5eUrl));
    if (m3u8Data.isEmpty()) {
        return QStringList();
    }

    // 解析质量信息
    QHash<QString, QString> qualityUrls = parseM3U8QualityUrls(m3u8Data, hlsH5eUrl);
    if (qualityUrls.isEmpty()) {
        return QStringList();
    }

    // 选择质量
    QString selectedQuality = selectQuality(quality, qualityUrls);
    if (selectedQuality.isEmpty()) {
        return QStringList();
    }

    // 获取TS文件列表
    return getTsFileList(qualityUrls[selectedQuality], hlsH5eUrl);
}

QHash<QString, QString> APIService::parseM3U8QualityUrls(const QByteArray& m3u8Data, const QString& baseUrl)
{
    QStringList m3u8Lines = QString::fromUtf8(m3u8Data).split("\n");
    QHash<QString, QualityInfo> qualityMap = {
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
            }
        }
        else if (!trimmedLine.startsWith("#") && !currentQuality.isEmpty() && !trimmedLine.isEmpty()) {
            qualityUrls[currentQuality] = trimmedLine;
            currentQuality.clear();
        }
    }

    return qualityUrls;
}

QString APIService::findQualityByBandwidth(const QHash<QString, QualityInfo>& qualityMap, int bandwidth)
{
    for (auto it = qualityMap.begin(); it != qualityMap.end(); ++it) {
        if (it.value().bandwidth == bandwidth) {
            return it.key();
        }
    }
    return QString();
}

QString APIService::selectQuality(const QString& requestedQuality, const QHash<QString, QString>& availableQualities)
{
    if (requestedQuality == "0") {
        // 自动选择最高质量
        QStringList qualities = availableQualities.keys();
        if (qualities.isEmpty()) {
            return QString();
        }
        return *std::max_element(qualities.begin(), qualities.end());
    }

    if (availableQualities.contains(requestedQuality)) {
        return requestedQuality;
    }

    qWarning() << "Requested quality not available:" << requestedQuality
        << "Available:" << availableQualities.keys().join(", ");
    return QString();
}

QStringList APIService::getTsFileList(const QString& qualityPath, const QString& baseUrl)
{
    QString m3u8Host = QUrl(baseUrl).host();
    QString fullM3u8Url = "https://" + m3u8Host + qualityPath;

    QByteArray videoM3u8Data = sendNetworkRequest(QUrl(fullM3u8Url));
    if (videoM3u8Data.isEmpty()) {
        return QStringList();
    }

    QStringList videoLines = QString::fromUtf8(videoM3u8Data).split("\n");
    QStringList tsList;
    QString basePath = fullM3u8Url.left(fullM3u8Url.lastIndexOf("/") + 1);

    for (const QString& line : videoLines) {
        if (line.endsWith(".ts")) {
            tsList << (basePath + line);
        }
    }

    return tsList;
}