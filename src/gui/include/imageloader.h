#pragma once

#include <QObject>
#include <QMutex>
#include <QPointer>
#include <QString>

class QImage;
class QNetworkAccessManager;

#ifdef CORE_REGRESSION_TESTS
class ImageLoaderTestAdapter;
#endif

class ImageLoader : public QObject
{
    Q_OBJECT

#ifdef CORE_REGRESSION_TESTS
    friend class ImageLoaderTestAdapter;
#endif

public:
    static ImageLoader& instance();

    ImageLoader(const ImageLoader&) = delete;
    ImageLoader& operator=(const ImageLoader&) = delete;

    quint64 startGetImage(const QString& url);

signals:
    void imageResolved(quint64 requestId, const QString& url, const QImage& image);

private:
    explicit ImageLoader(QObject* parent = nullptr);
    ~ImageLoader() override;

    QImage getImage(const QString& url);
    QByteArray sendNetworkRequest(const QUrl& url);
    quint64 nextRequestId();

#ifdef CORE_REGRESSION_TESTS
    void setTestNetworkAccessManager(QNetworkAccessManager* networkAccessManager);
    void clearTestNetworkAccessManager();
#endif

    static QPointer<ImageLoader> m_instance;
    static QMutex m_instanceMutex;

    QMutex m_mutex;
    QNetworkAccessManager* m_networkAccessManager = nullptr;
    quint64 m_nextRequestId = 0;
    quint64 m_activeRequestId = 0;

#ifdef CORE_REGRESSION_TESTS
    QPointer<QNetworkAccessManager> m_testNetworkAccessManager;
#endif
};
