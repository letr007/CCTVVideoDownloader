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
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>

// 视频信息
struct VideoItem {
    QString guid;
    QString time;
    QString title;
    QString image;
    QString brief;
    bool isHighlight = false;
    QString listType = QStringLiteral("完整");

    VideoItem(QString g, QString t, QString ti, QString i, QString b, bool h = false, QString lt = QStringLiteral("完整"))
        : guid(std::move(g)), time(std::move(t)), title(std::move(ti)),
        image(std::move(i)), brief(std::move(b)), isHighlight(h), listType(std::move(lt)) {
    }

    VideoItem() = default;
};

Q_DECLARE_METATYPE(VideoItem)
using VideoItemMap = QMap<int, VideoItem>;
Q_DECLARE_METATYPE(VideoItemMap)

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

#ifdef CORE_REGRESSION_TESTS
    friend class APIServiceTestAdapter;
#endif

public:
    // 线程安全的单例访问
    static APIService& instance();

    // 删除拷贝构造和赋值
    APIService(const APIService&) = delete;
    APIService& operator=(const APIService&) = delete;

    // API接口
    // Browse/import synchronous compatibility wrappers. Keep these out of the active
    // download/post-processing path and prefer the async browse methods from the UI.
    QSharedPointer<QStringList> getPlayColumnInfo(const QString& url);
    QMap<int, VideoItem> getVideoList(const QString& column_id, const QString& item_id, const QString& start_date, const QString& end_date);
    QMap<int, VideoItem> getHighlightList(const QString& item_id);
    QMap<int, VideoItem> getFragmentList(const QString& column_id, const QString& item_id);
    //QMap<int, VideoItem> getVideoList(const QString& column_id, const QString& item_id, int start_index, int end_index);
    QImage getImage(const QString& url);
    quint64 startGetPlayColumnInfo(const QString& url);
    quint64 startGetBrowseVideoList(const QString& column_id, const QString& item_id, const QString& start_date, const QString& end_date, bool includeHighlights);
    quint64 startGetImage(const QString& url);
    void startGetEncryptM3U8Urls(const QString& GUID, const QString& quality);
    void cancelGetEncryptM3U8Urls();
    bool lastM3U8ResultWas4K() const;

signals:
    void PlayColumnInfoCallback(const QStringList& data);
    void playColumnInfoResolved(quint64 requestId, const QStringList& data);
    void playColumnInfoFailed(quint64 requestId, const QString& errorMessage);
    void browseVideoListResolved(quint64 requestId, const QMap<int, VideoItem>& videos);
    void imageResolved(quint64 requestId, const QString& url, const QImage& image);
    void encryptM3U8UrlsResolved(const QStringList& urls, bool is4K);
    void encryptM3U8UrlsFailed(const QString& errorMessage);
    void encryptM3U8UrlsCancelled();

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
    QUrl buildAlbumVideoListUrl(const QString& album_id, int mode, int page, int page_size);
    QUrl buildTopicVideoListUrl(const QString& column_id, const QString& item_id, int type);

    // 数据处理方法
    void processMonthData(const QJsonArray& items, const QString& month, QMap<int, VideoItem>& result, int& result_index, bool isHighlight = false, const QString& listType = QStringLiteral("完整"));
    void processTopicVideoData(const QJsonArray& items, QMap<int, VideoItem>& result, int& result_index);

    // M3U8相关方法
    QHash<QString, QString> parseM3U8QualityUrls(const QByteArray& m3u8Data, const QString& baseUrl);
    QString findQualityByBandwidth(const QHash<QString, QualityInfo>& qualityMap, int bandwidth);
    QString selectQuality(const QString& requestedQuality, const QHash<QString, QString>& availableQualities);
    QStringList buildTsUrlsFromPlaylistData(const QByteArray& videoM3u8Data, const QString& fullM3u8Url);
    QStringList getTsFileList(const QString& qualityPath, const QString& baseUrl);
    QNetworkAccessManager* networkAccessManager();
    QNetworkRequest buildNetworkRequest(const QUrl& url, const QHash<QString, QString>& headers = QHash<QString, QString>()) const;
    void startM3u8NetworkRequest(quint64 requestId, const QUrl& url);
    void handleM3u8ReplyFinished(quint64 requestId);
    void finishM3u8ResolveSuccess(quint64 requestId, const QStringList& urls, bool is4K);
    void finishM3u8ResolveFailure(quint64 requestId, const QString& errorMessage);
    QString normalizeEncryptedM3u8Url(QString hlsH5eUrl) const;
    quint64 nextAsyncBrowseRequestId();

#ifdef CORE_REGRESSION_TESTS
    void setTestNetworkAccessManager(QNetworkAccessManager* networkAccessManager);
    void clearTestNetworkAccessManager();
    QStringList getEncryptM3U8Urls(const QString& GUID, const QString& quality);
#endif




private:
    enum class M3u8ResolveStage {
        None,
        FetchInfo,
        Fetch4KPlaylist,
        FetchMasterPlaylist,
        FetchVariantPlaylist
    };

    static QPointer<APIService> m_instance;
    static QMutex m_instanceMutex;

    QMutex m_mutex;
    bool m_lastM3U8ResultWas4K = false;
    QNetworkAccessManager m_networkAccessManager;
    QPointer<QNetworkReply> m_pendingM3u8Reply;
    quint64 m_activeM3u8ResolveId = 0;
    quint64 m_nextAsyncBrowseRequestId = 0;
    quint64 m_activePlayColumnInfoRequestId = 0;
    quint64 m_activeBrowseVideoListRequestId = 0;
    quint64 m_activeImageRequestId = 0;
    M3u8ResolveStage m_m3u8ResolveStage = M3u8ResolveStage::None;
    QString m_pendingGuid;
    QString m_pendingQuality;
    QString m_pendingMasterPlaylistUrl;

#ifdef CORE_REGRESSION_TESTS
    QPointer<QNetworkAccessManager> m_testNetworkAccessManager;
#endif
};

