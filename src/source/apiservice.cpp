#include"../head/apiservice.h"
#include <QCoreApplication>
#include <cpr/cpr.h>
#include <algorithm>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QImage>

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
	//connect(&m_network, &NetworkCore::responseReceived, this, &APIService::handlePlayColumnInfo, Qt::QueuedConnection);
}

APIService::~APIService()
{ }

QSharedPointer<QStringList> APIService::getPlayColumnInfo(const QString& url) {
    // 发起请求
    std::string encodedUrl = url.toUtf8().constData();
    cpr::Response r = cpr::Get(cpr::Url{ encodedUrl });

    // 检查响应
    if (r.status_code != 200) {
        qWarning() << "请求失败 | URL:" << url << "| 状态码:" << r.status_code;
        return nullptr;
    }

    // 转换响应数据
    QString html = QString::fromUtf8(r.text.data(), r.text.size());
    auto results = QSharedPointer<QStringList>::create();

    // 预编译正则表达式
    static const QRegularExpression title_regex(R"(var commentTitle\s*=\s*["'](.*?)["'];)");
    static const QRegularExpression itemid_regex(R"(var itemid1\s*=\s*["'](.*?)["'];)");
    static const QRegularExpression column_regex(R"(var column_id\s*=\s*["'](.*?)["'];)");

    // 安全匹配
    auto safeMatch = [](const QRegularExpression& regex, const QString& text) -> QString {
        auto match = regex.match(text);
        if (match.hasMatch()) {
            // 显式地将 QByteArray 转换为 QString
            // QString(const QByteArray& a) 构造函数会复制数据
            //qDebug() << "Captured:" << match.captured(1);
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

    //results << QString("测试标题") << QString("VIDE") << QString("TOPC");

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

    // 计算页数范围
    constexpr int page_size = 100;  // 编译期常量
    const int start_page = start_index / page_size + 1;
    const int end_page = end_index / page_size + 1;

    QMap<int, VideoItem> result;

    const cpr::Url base_url{ "https://api.cntv.cn/NewVideo/getVideoListByColumn" };

    // 遍历每一页
    for (int page = start_page; page <= end_page; ++page) {
        const int page_start_index = (page - 1) * page_size;

        const cpr::Parameters params{
            {"id", column_id.toUtf8().constData()},  // 确保参数传递
            {"n", std::to_string(page_size)},
            {"sort", "desc"},
            {"p", std::to_string(page)},
            {"mode", "0"},
            {"serviceId", "tvcctv"}
        };

        // 发送同步HTTP请求
        cpr::Response r = cpr::Get(base_url, params);

        if (r.status_code != 200) {
            qWarning() << "请求失败:" << base_url.data();
            continue;  // 跳过失败请求
        }

        // 解析JSON
        const QJsonDocument doc = QJsonDocument::fromJson(
            r.text.data()
        );
        const QJsonArray items = doc.object()["data"].toObject()["list"].toArray();

        // 处理当前页数据
        //qDebug() << "Items count:" << items.size();
        for (int i = 0; i < items.size(); ++i) {
            const int current_index = page_start_index + i;
            if (current_index < start_index || current_index > end_index) {
                continue;
            }

            const QJsonObject item = items[i].toObject();
            //qDebug() << "Item fields:" << item.keys();
            result.insert(current_index, VideoItem{
                item["guid"].toString(),
                item["time"].toString(),
                item["title"].toString(),
                item["image"].toString(),
                item["brief"].toString()
                });
            //qDebug() << item["guid"].toString() << item["time"].toString() << item["image"].toString();
        }
    }

    return result;
}

QImage APIService::getImage(const QString& url)
{
    // 1. 发送HTTP请求
    cpr::Response r = cpr::Get(cpr::Url{ url.toUtf8().constData()});

    // 2. 检查HTTP响应状态
    if (r.status_code != 200) {
        qWarning() << "获取图片失败，HTTP状态码:" << r.status_code
            << "URL:" << url;
        return QImage(); // 返回空QImage
    }

    // 3. 检查返回数据是否为空
    if (r.text.empty()) {
        qWarning() << "图片数据为空，URL:" << url;
        return QImage();
    }

    // 4. 将二进制数据转换为QImage
    QImage image;
    if (!image.loadFromData(
        reinterpret_cast<const uchar*>(r.text.data()),
        static_cast<int>(r.text.size())))
    {
        qWarning() << "图片数据解析失败，可能不是有效的图像格式，URL:" << url;
        return QImage();
    }

    // 5. 可选：转换为更高效的格式
    if (image.format() != QImage::Format_ARGB32 &&
        image.format() != QImage::Format_RGB32)
    {
        image = image.convertToFormat(QImage::Format_ARGB32);
    }

    return image;
}

QStringList APIService::getEncryptM3U8Urls(const QString& GUID, const QString& quality)
{
    // 发送请求
    const cpr::Url base_url = { "https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do" };
    const cpr::Parameters params{
        {"pid", GUID.toUtf8().constData()}
    };
    cpr::Response r = cpr::Get(base_url, params);
    if (r.status_code != 200)
    {
        qWarning() << "获取加密视频下链接失败";
        return QStringList();
    }

    // 解析JSON，提取hls链接
    const QJsonDocument doc = QJsonDocument::fromJson(r.text.data());
    const QString hlsH5eUrl = doc.object()["manifest"].toObject()["hls_enc2_url"].toString();
    if (hlsH5eUrl.isEmpty())
    {
        qWarning() << "hls_enc2_url 获取失败";
        return QStringList();
    }

    // 获取主M3U8文件
    cpr::Response mainM3u8Resp = cpr::Get(cpr::Url{ hlsH5eUrl.toUtf8().constData() });
    if (mainM3u8Resp.status_code != 200)
    {
        qWarning() << "获取主M3U8失败，状态码：" << mainM3u8Resp.status_code;
        return QStringList();
    }

    QStringList m3u8Lines = QString::fromUtf8(mainM3u8Resp.text.data()).split("\n");

    // 质量映射表
    QHash<QString, QualityInfo> quality_map = {
        {"4", {460800, "480x270"}},
        {"3", {870400, "640x360"}},
        {"2", {1228800, "1280x720"}},
        {"1", {2048000, "1280x720"}}
    };

    QHash<QString, QString> qualityUrls;
    QString currentQuality;

    for (int i = 0; i < m3u8Lines.size(); ++i)
    {
        const QString& line = m3u8Lines[i].trimmed();
        if (line.startsWith("#EXT-X-STREAM-INF"))
        {
            QRegularExpression re("BANDWIDTH=(\\d+)");
            QRegularExpressionMatch match = re.match(line);
            if (match.hasMatch())
            {
                int bandwidth = match.captured(1).toInt();
                for (auto it = quality_map.begin(); it != quality_map.end(); ++it)
                {
                    if (it.value().bandwidth == bandwidth)
                    {
                        currentQuality = it.key();
                        break;
                    }
                }
            }
        }
        else if (!line.startsWith("#") && !currentQuality.isEmpty())
        {
            qualityUrls[currentQuality] = line;
            currentQuality.clear();
        }
    }

    QString selectedQuality;
    if (quality == "0")
    {
        QStringList allQualities = qualityUrls.keys();
        if (allQualities.isEmpty()) {
            qWarning() << "未找到任何可用清晰度";
            return QStringList();
        }
        selectedQuality = *std::max_element(allQualities.begin(), allQualities.end(), [](const QString& a, const QString& b) {
            return a.toInt() < b.toInt();
            });
    }
    else
    {
        if (!qualityUrls.contains(quality))
        {
            qWarning() << "不支持的清晰度：" << quality << "，支持的清晰度有：" << qualityUrls.keys().join(", ");
            return QStringList();
        }
        selectedQuality = quality;
    }

    QString qualityPath = qualityUrls[selectedQuality];
    QString m3u8Host = QUrl(hlsH5eUrl).host();
    QString fullM3u8Url = "https://" + m3u8Host + qualityPath;

    /*qInfo() << "选择清晰度:" << selectedQuality << ", URL:" << fullM3u8Url;*/

    // 获取目标清晰度的 M3U8 文件
    cpr::Response videoM3u8Resp = cpr::Get(cpr::Url{ fullM3u8Url.toUtf8().constData() });
    if (videoM3u8Resp.status_code != 200)
    {
        qWarning() << "获取视频 ts 列表失败，状态码：" << videoM3u8Resp.status_code;
        return QStringList();
    }

    QStringList videoLines = QString::fromUtf8(videoM3u8Resp.text.data()).split("\n");
    QStringList tsList;
    for (const QString& line : videoLines)
    {
        if (line.endsWith(".ts"))
        {
            QString tsUrl = fullM3u8Url.left(fullM3u8Url.lastIndexOf("/") + 1) + line;
            tsList << tsUrl;
        }
    }

    return tsList;
}

