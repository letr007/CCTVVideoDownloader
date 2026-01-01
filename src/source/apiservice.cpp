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
    qInfo() << "发送网络请求:" << url.toString();
    
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    // 设置SSL配置以绕过SSL验证
    QSslConfiguration sslConfig = request.sslConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(sslConfig);

    // 设置User-Agent
    request.setHeader(QNetworkRequest::UserAgentHeader,
        "Lavf/60.10.100");

    // 添加自定义头部
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    QNetworkReply* reply = manager.get(request);
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
        qWarning() << "获取视频列表失败: 所有方式都未获取到数据";
    }

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

    // 按月循环
    for (const QString& date : dateList) {
        qInfo() << "处理月份:" << date << "格式:yyyyMM";

        // 构建API URL
        QUrl url = buildVideoApiUrl(fetch_type, id, date, 1, 100);
        qInfo() << "请求URL:" << url.toString();

        QByteArray responseData = sendNetworkRequest(url);

        if (responseData.isEmpty()) {
            qWarning() << "月份" << date << "获取数据失败";
            continue;
        }

        QJsonArray items = parseJsonArray(responseData, "data", "list");
        if (items.isEmpty()) {
            qWarning() << "月份" << date << "数据为空";
            continue;
        }

        qInfo() << "月份" << date << "获取到" << items.size() << "个项目";

        // 处理当前月数据
        processMonthData(items, date, result, result_index);

        // 处理事件循环
        QCoreApplication::processEvents();
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

QJsonObject APIService::parseJsonObject(const QByteArray& data, const QString& key)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "JSON解析失败:" << parseError.errorString();
        return QJsonObject();
    }

    QJsonObject rootObj = doc.object();
    QJsonObject result = rootObj.contains(key) ? rootObj[key].toObject() : QJsonObject();
    
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
    int& result_index)
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

QStringList APIService::getEncryptM3U8Urls(const QString& GUID, const QString& quality)
{
    qInfo() << "获取加密M3U8 URL，GUID:" << GUID << "质量:" << quality;
    
    // 获取视频信息
    QUrl infoUrl("https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do");
    QUrlQuery infoQuery;
    infoQuery.addQueryItem("pid", GUID);
    infoUrl.setQuery(infoQuery);

    QByteArray infoData = sendNetworkRequest(infoUrl);
    if (infoData.isEmpty()) {
        qWarning() << "获取视频信息失败: 响应数据为空";
        return QStringList();
    }

    QJsonObject manifestObj = parseJsonObject(infoData, "manifest");
    QString hlsH5eUrl = manifestObj["hls_enc2_url"].toString();

    if (hlsH5eUrl.isEmpty()) {
        qWarning() << "无法获取hls_enc2_url";
        return QStringList();
    }

    qInfo() << "获取到M3U8 URL:" << hlsH5eUrl;
    
    // 替换CDN
    QRegularExpression re("https://[^/]+/asp/enc2/");
    QRegularExpressionMatch match = re.match(hlsH5eUrl);

    if (match.hasMatch()) {
        // hlsH5eUrl.replace(match.captured(0), "https://dh5cntv.a.bdydns.com/asp/enc2/");
        // replace CDN
        // drm.cntv.vod.dnsv1.com
        // dhls.cntv.baishancdnx.cn.bsgslb.cn
        hlsH5eUrl.replace(match.captured(0), "https://drm.cntv.vod.dnsv1.com/asp/enc2/");
    }
    else {
        qWarning() << "无法替换CDN，使用默认CDN";
    }

    qInfo() << "替换后M3U8 URL:" << hlsH5eUrl;

    // 获取主M3U8文件
    QByteArray m3u8Data = sendNetworkRequest(QUrl(hlsH5eUrl));
    if (m3u8Data.isEmpty()) {
        qWarning() << "获取M3U8文件失败: 响应数据为空";
        return QStringList();
    }

    qDebug() << "M3U8文件大小:" << m3u8Data.size() << "字节";

    // 解析质量信息
    QHash<QString, QString> qualityUrls = parseM3U8QualityUrls(m3u8Data, hlsH5eUrl);
    if (qualityUrls.isEmpty()) {
        qWarning() << "解析M3U8质量信息失败";
        return QStringList();
    }

    qDebug() << "可用的质量选项:" << qualityUrls.keys().join(", ");

    // 选择质量
    QString selectedQuality = selectQuality(quality, qualityUrls);
    if (selectedQuality.isEmpty()) {
        qWarning() << "选择质量失败";
        return QStringList();
    }

    qDebug() << "选择的质量:" << selectedQuality;

    // 获取TS文件列表
    QStringList tsList = getTsFileList(qualityUrls[selectedQuality], hlsH5eUrl);
    qDebug() << "获取到" << tsList.size() << "个TS文件";
    
    return tsList;
}

QHash<QString, QString> APIService::parseM3U8QualityUrls(const QByteArray& m3u8Data, const QString& baseUrl)
{
    qDebug() << "解析M3U8质量URL";
    
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
    return QString();
}

QString APIService::selectQuality(const QString& requestedQuality, const QHash<QString, QString>& availableQualities)
{
    qDebug() << "选择质量，请求质量:" << requestedQuality;
    
    if (requestedQuality == "0") {
        // 自动选择最高质量
        QStringList qualities = availableQualities.keys();
        if (qualities.isEmpty()) {
            qWarning() << "自动选择质量失败: 无可用的质量选项";
            return QString();
        }
        QString selected = *std::max_element(qualities.begin(), qualities.end());
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

    qDebug() << "视频M3U8文件大小:" << videoM3u8Data.size() << "字节";

    QStringList videoLines = QString::fromUtf8(videoM3u8Data).split("\n");
    QStringList tsList;
    QString basePath = fullM3u8Url.left(fullM3u8Url.lastIndexOf("/") + 1);

    for (const QString& line : videoLines) {
        if (line.endsWith(".ts")) {
            QString tsUrl = basePath + line;
            tsList << tsUrl;
        }
    }

    qDebug() << "解析完成，找到" << tsList.size() << "个TS文件";
    
    return tsList;
}////////////////////////