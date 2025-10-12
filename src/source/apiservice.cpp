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

APIService& APIService::instance() {
    static QPointer<APIService> instance;
    static QMutex mutex;

    if (instance.isNull()) {
        QMutexLocker locker(&mutex);
        if (instance.isNull()) {
            instance = new APIService(qApp);
        }
    }
    return *instance;
}

APIService::APIService(QObject* parent) : QObject(parent)
{
    // 强制跨线程队列化，确保回调在主线程执行
    // connect(&m_network, &NetworkCore::responseReceived, this, &APIService::handlePlayColumnInfo, Qt::QueuedConnection);
}

APIService::~APIService()
{
}

QSharedPointer<QStringList> APIService::getPlayColumnInfo(const QString& url) {
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");

    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "请求失败 | URL:" << url << "| 错误:" << reply->errorString();
        reply->deleteLater();
        return nullptr;
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    QString html = QString::fromUtf8(responseData);
    auto results = QSharedPointer<QStringList>::create();

    // 预编译正则表达式
    static const QRegularExpression title_regex(R"(var commentTitle\s*=\s*["'](.*?)["'];)");
    static const QRegularExpression itemid_regex(R"(var itemid1\s*=\s*["'](.*?)["'];)");
    static const QRegularExpression column_regex(R"(var column_id\s*=\s*["'](.*?)["'];)");

    // 安全匹配
    auto safeMatch = [](const QRegularExpression& regex, const QString& text) -> QString {
        auto match = regex.match(text);
        if (match.hasMatch()) {
            return QString(match.captured(1).toUtf8());
        }
        else {
            return QString();
        }
        };

    // 填充结果
    results->append(safeMatch(title_regex, html).split(" ").at(0));
    results->append(safeMatch(itemid_regex, html));
    results->append(safeMatch(column_regex, html));

    // 验证数据完整性
    if (results->contains(QString())) {
        qWarning() << "部分匹配失败 | 结果:" << results;
        return nullptr;
    }

    return results;
}

QMap<int, VideoItem> APIService::getVideoList(
    const QString& column_id,
    const QString& item_id,
    int start_index,
    int end_index)
{
    // 参数校验和交换
    if (start_index < 0 || end_index < 0) {
        return {};
    }
    if (start_index > end_index) {
        std::swap(start_index, end_index);
    }

    // 限制请求范围，防止内存爆炸
    const int MAX_REQUEST_COUNT = 1000;
    if (end_index - start_index > MAX_REQUEST_COUNT) {
        end_index = start_index + MAX_REQUEST_COUNT;
        qWarning() << "请求范围过大，已自动限制为" << MAX_REQUEST_COUNT << "条记录";
    }

    // 计算页数范围
    constexpr int page_size = 100;
    const int start_page = start_index / page_size + 1;
    const int end_page = end_index / page_size + 1;

    QMap<int, VideoItem> result;
    int result_index = 0;

    QNetworkAccessManager manager;

    // 遍历每一页
    for (int page = start_page; page <= end_page; ++page) {
        const int page_start_index = (page - 1) * page_size;

        // 构建URL和参数
        QUrl url("https://api.cntv.cn/NewVideo/getVideoListByColumn");
        QUrlQuery query;
        query.addQueryItem("id", column_id);
        query.addQueryItem("n", QString::number(page_size));
        query.addQueryItem("sort", "desc");
        query.addQueryItem("p", QString::number(page));
        query.addQueryItem("mode", "0");
        query.addQueryItem("serviceId", "tvcctv");
        url.setQuery(query);

        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");

        QNetworkReply* reply = manager.get(request);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "请求失败:" << reply->errorString() << "URL:" << url.toString();
            reply->deleteLater();
            continue;
        }

        QByteArray responseData = reply->readAll();
        reply->deleteLater();

        if (responseData.isEmpty()) {
            qWarning() << "响应数据为空";
            continue;
        }

        // 解析JSON
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);

        if (parseError.error != QJsonParseError::NoError) {
            qWarning() << "JSON解析错误:" << parseError.errorString();
            continue;
        }

        if (!doc.isObject()) {
            qWarning() << "JSON根节点不是对象";
            continue;
        }

        QJsonObject rootObj = doc.object();
        if (!rootObj.contains("data")) {
            qWarning() << "JSON缺少data字段";
            continue;
        }

        QJsonObject dataObj = rootObj["data"].toObject();
        if (!dataObj.contains("list")) {
            qWarning() << "JSON data缺少list字段";
            continue;
        }

        QJsonArray items = dataObj["list"].toArray();

        // 处理当前页数据
        for (int i = 0; i < items.size(); i++) {
            const int current_global_index = page_start_index + i;

            // 检查是否在请求的索引范围内
            if (current_global_index < start_index || current_global_index > end_index) {
                continue;
            }

            QJsonObject item = items[i].toObject();

            // 检查必要字段
            if (!item.contains("guid") || !item.contains("title")) {
                qWarning() << "跳过缺少必要字段的数据项";
                continue;
            }

            // 使用从0开始的连续索引
            result.insert(result_index, VideoItem{
                item["guid"].toString(),
                item["time"].toString(),
                item["title"].toString(),
                item["image"].toString(),
                item["brief"].toString()
                });

            result_index++;
        }

        // 每处理完一页，强制垃圾回收
        QCoreApplication::processEvents();
    }

    return result;
}

QImage APIService::getImage(const QString& url)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");

    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "获取图片失败，错误:" << reply->errorString() << "URL:" << url;
        reply->deleteLater();
        return QImage();
    }

    QByteArray imageData = reply->readAll();
    reply->deleteLater();

    if (imageData.isEmpty()) {
        qWarning() << "图片数据为空，URL:" << url;
        return QImage();
    }

    QImage image;
    if (!image.loadFromData(imageData)) {
        qWarning() << "图片数据解析失败，可能不是有效的图像格式，URL:" << url;
        return QImage();
    }

    // 转换为更高效的格式
    if (image.format() != QImage::Format_ARGB32 &&
        image.format() != QImage::Format_RGB32) {
        image = image.convertToFormat(QImage::Format_ARGB32);
    }

    return image;
}

QStringList APIService::getEncryptM3U8Urls(const QString& GUID, const QString& quality)
{
    // 构建URL和参数
    QUrl url("https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do");
    QUrlQuery query;
    query.addQueryItem("pid", GUID);
    url.setQuery(query);

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");

    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "获取加密视频链接失败，错误:" << reply->errorString();
        reply->deleteLater();
        return QStringList();
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    // 解析JSON，提取hls链接
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "JSON解析错误:" << parseError.errorString();
        return QStringList();
    }

    const QString hlsH5eUrl = doc.object()["manifest"].toObject()["hls_enc2_url"].toString();
    if (hlsH5eUrl.isEmpty()) {
        qWarning() << "hls_enc2_url 获取失败";
        return QStringList();
    }

    // 获取主M3U8文件
    QNetworkRequest m3u8Request(hlsH5eUrl);
    m3u8Request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");

    QNetworkReply* m3u8Reply = manager.get(m3u8Request);
    QEventLoop m3u8Loop;
    QObject::connect(m3u8Reply, &QNetworkReply::finished, &m3u8Loop, &QEventLoop::quit);
    m3u8Loop.exec();

    if (m3u8Reply->error() != QNetworkReply::NoError) {
        qWarning() << "获取主M3U8失败，错误:" << m3u8Reply->errorString();
        m3u8Reply->deleteLater();
        return QStringList();
    }

    QByteArray m3u8Data = m3u8Reply->readAll();
    m3u8Reply->deleteLater();

    QStringList m3u8Lines = QString::fromUtf8(m3u8Data).split("\n");

    // 质量映射表
    QHash<QString, QualityInfo> quality_map = {
        {"4", {460800, "480x270"}},
        {"3", {870400, "640x360"}},
        {"2", {1228800, "1280x720"}},
        {"1", {2048000, "1280x720"}}
    };

    QHash<QString, QString> qualityUrls;
    QString currentQuality;

    for (int i = 0; i < m3u8Lines.size(); ++i) {
        const QString& line = m3u8Lines[i].trimmed();
        if (line.startsWith("#EXT-X-STREAM-INF")) {
            QRegularExpression re("BANDWIDTH=(\\d+)");
            QRegularExpressionMatch match = re.match(line);
            if (match.hasMatch()) {
                int bandwidth = match.captured(1).toInt();
                for (auto it = quality_map.begin(); it != quality_map.end(); ++it) {
                    if (it.value().bandwidth == bandwidth) {
                        currentQuality = it.key();
                        break;
                    }
                }
            }
        }
        else if (!line.startsWith("#") && !currentQuality.isEmpty()) {
            qualityUrls[currentQuality] = line;
            currentQuality.clear();
        }
    }

    QString selectedQuality;
    if (quality == "0") {
        QStringList allQualities = qualityUrls.keys();
        if (allQualities.isEmpty()) {
            qWarning() << "未找到任何可用清晰度";
            return QStringList();
        }
        selectedQuality = *std::max_element(allQualities.begin(), allQualities.end(), [](const QString& a, const QString& b) {
            return a.toInt() < b.toInt();
            });
    }
    else {
        if (!qualityUrls.contains(quality)) {
            qWarning() << "不支持的清晰度：" << quality << "，支持的清晰度有：" << qualityUrls.keys().join(", ");
            return QStringList();
        }
        selectedQuality = quality;
    }

    QString qualityPath = qualityUrls[selectedQuality];
    QString m3u8Host = QUrl(hlsH5eUrl).host();
    QString fullM3u8Url = "https://" + m3u8Host + qualityPath;

    // 获取目标清晰度的 M3U8 文件
    QNetworkRequest videoM3u8Request(fullM3u8Url);
    videoM3u8Request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");

    QNetworkReply* videoM3u8Reply = manager.get(videoM3u8Request);
    QEventLoop videoM3u8Loop;
    QObject::connect(videoM3u8Reply, &QNetworkReply::finished, &videoM3u8Loop, &QEventLoop::quit);
    videoM3u8Loop.exec();

    if (videoM3u8Reply->error() != QNetworkReply::NoError) {
        qWarning() << "获取视频 ts 列表失败，错误:" << videoM3u8Reply->errorString();
        videoM3u8Reply->deleteLater();
        return QStringList();
    }

    QByteArray videoM3u8Data = videoM3u8Reply->readAll();
    videoM3u8Reply->deleteLater();

    QStringList videoLines = QString::fromUtf8(videoM3u8Data).split("\n");
    QStringList tsList;
    for (const QString& line : videoLines) {
        if (line.endsWith(".ts")) {
            QString tsUrl = fullM3u8Url.left(fullM3u8Url.lastIndexOf("/") + 1) + line;
            tsList << tsUrl;
        }
    }

    return tsList;
}