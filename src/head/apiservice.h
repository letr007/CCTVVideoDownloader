#pragma once

#include <QObject>
#include <QPointer>
#include <QMutex>
#include <QString>
#include <QHash>
#include <QMap>
#include <QSharedPointer>
#include <QStringList>
#include <QImage>
#include <QJsonArray>
#include <QUrl>

// 视频信息
struct VideoItem {
    QString guid;
    QString time;
    QString title;
    QString image;
    QString brief;

    VideoItem(QString g, QString t, QString ti, QString i, QString b)
        : guid(std::move(g)), time(std::move(t)), title(std::move(ti)),
        image(std::move(i)), brief(std::move(b)) {
    }

    VideoItem() = default;
};

// M3U8质量信息
struct QualityInfo {
    int bandwidth;
    QString resolution;

    QualityInfo(int bw = 0, const QString& res = QString())
        : bandwidth(bw), resolution(res) {
    }
};

enum class FetchType {
    Column,
    Album
};

class APIService : public QObject
{
    Q_OBJECT

public:
    // 线程安全的单例访问
    static APIService& instance();

    // 删除拷贝构造和赋值
    APIService(const APIService&) = delete;
    APIService& operator=(const APIService&) = delete;

    // API接口
    QSharedPointer<QStringList> getPlayColumnInfo(const QString& url);
    QMap<int, VideoItem> getVideoList(const QString& column_id, const QString& item_id, const QString& start_date, const QString& end_date);
    //QMap<int, VideoItem> getVideoList(const QString& column_id, const QString& item_id, int start_index, int end_index);
    QImage getImage(const QString& url);
    QStringList getEncryptM3U8Urls(const QString& GUID, const QString& quality);

signals:
    void PlayColumnInfoCallback(const QStringList& data);

private:
    // 构造函数和析构函数
    explicit APIService(QObject* parent = nullptr);
    ~APIService();

    // 内部辅助方法
    QString getRealAlbumId(const QString& item_id);
    QMap<int, VideoItem> fetchVideoData(const QString& id, const QStringList dataList, FetchType fetch_type);
    QByteArray sendNetworkRequest(const QUrl& url, const QHash<QString, QString>& headers = QHash<QString, QString>());    

    // JSON解析相关方法
    QJsonObject parseJsonObject(const QByteArray& data, const QString& key = QString());
    QJsonArray parseJsonArray(const QByteArray& data, const QString& objectKey = QString(), const QString& arrayKey = QString());

    // URL构建方法
    QUrl buildVideoApiUrl(FetchType fetch_type, const QString& id, const QString& date, int page, int page_size);

    // 数据处理方法
    void processMonthData(const QJsonArray& items, const QString& month, QMap<int, VideoItem>& result, int& result_index);

    // M3U8相关方法
    QHash<QString, QString> parseM3U8QualityUrls(const QByteArray& m3u8Data, const QString& baseUrl);
    QString findQualityByBandwidth(const QHash<QString, QualityInfo>& qualityMap, int bandwidth);
    QString selectQuality(const QString& requestedQuality, const QHash<QString, QString>& availableQualities);
    QStringList getTsFileList(const QString& qualityPath, const QString& baseUrl);

private:
    static QPointer<APIService> m_instance;
    static QMutex m_instanceMutex;

    QMutex m_mutex;
};