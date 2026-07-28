#include "imageloader.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QImage>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QThread>
#include <QTimer>
#include <QUrl>

QPointer<ImageLoader> ImageLoader::m_instance = nullptr;
QMutex ImageLoader::m_instanceMutex;

ImageLoader& ImageLoader::instance()
{
    if (m_instance.isNull()) {
        QMutexLocker locker(&m_instanceMutex);
        if (m_instance.isNull()) {
            m_instance = new ImageLoader(qApp);
        }
    }
    return *m_instance;
}

ImageLoader::ImageLoader(QObject* parent)
    : QObject(parent)
    , m_networkAccessManager(new QNetworkAccessManager(this))
{
}

ImageLoader::~ImageLoader() = default;

quint64 ImageLoader::nextRequestId()
{
    QMutexLocker locker(&m_mutex);
    return ++m_nextRequestId;
}

QByteArray ImageLoader::sendNetworkRequest(const QUrl& url)
{
    QNetworkAccessManager localManager;
    QNetworkAccessManager* manager = &localManager;
#ifdef CORE_REGRESSION_TESTS
    if (m_testNetworkAccessManager) {
        manager = m_testNetworkAccessManager;
    }
#endif

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

    QNetworkReply* reply = manager->get(request);
    connect(reply, &QNetworkReply::errorOccurred, [reply](QNetworkReply::NetworkError error) {
        if (error == QNetworkReply::SslHandshakeFailedError) {
            reply->ignoreSslErrors();
        }
    });
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray imageData = reply->error() == QNetworkReply::NoError
        ? reply->readAll()
        : QByteArray();
    reply->deleteLater();
    return imageData;
}

QImage ImageLoader::getImage(const QString& url)
{
    const QByteArray imageData = sendNetworkRequest(QUrl(url));
    if (imageData.isEmpty()) {
        return QImage();
    }

    QImage image;
    if (!image.loadFromData(imageData)) {
        return QImage();
    }

    if (image.format() != QImage::Format_ARGB32
        && image.format() != QImage::Format_RGB32) {
        image = image.convertToFormat(QImage::Format_ARGB32);
    }
    return image;
}

quint64 ImageLoader::startGetImage(const QString& url)
{
    const quint64 requestId = nextRequestId();
    {
        QMutexLocker locker(&m_mutex);
        m_activeRequestId = requestId;
    }

    auto publishResult = [this, requestId, url]() {
        const QImage image = getImage(url);
        const bool matchesActiveRequest = [this, requestId]() {
            QMutexLocker locker(&m_mutex);
            return m_activeRequestId == requestId;
        }();
        if (matchesActiveRequest) {
            emit imageResolved(requestId, url, image);
        }
    };

#ifdef CORE_REGRESSION_TESTS
    if (m_testNetworkAccessManager) {
        QTimer::singleShot(0, this, publishResult);
        return requestId;
    }
#endif

    QThread* workerThread = QThread::create(publishResult);
    workerThread->setObjectName(QStringLiteral("ImageLoaderWorker"));
    connect(workerThread, &QThread::finished, workerThread, &QObject::deleteLater);
    workerThread->start();
    return requestId;
}

#ifdef CORE_REGRESSION_TESTS
void ImageLoader::setTestNetworkAccessManager(QNetworkAccessManager* networkAccessManager)
{
    m_testNetworkAccessManager = networkAccessManager;
}

void ImageLoader::clearTestNetworkAccessManager()
{
    m_testNetworkAccessManager = nullptr;
}
#endif
