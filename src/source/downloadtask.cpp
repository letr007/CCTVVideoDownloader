#include "../head/downloadtask.h"
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QEventLoop>
#include <QFile>
#include <QDir>
#include <QUrl>
#include <QDateTime>
#include <QDebug>
#include <QPointer>
#include <QTimer>

static QString extractFilenameFromUrlImpl(const QString& url) {
    QUrl qurl(url);
    QString path = qurl.path();
    QString filename = path.mid(path.lastIndexOf('/') + 1);
    if (filename.isEmpty()) {
        filename = "download_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".dat";
    }
    return filename;
}

static int normalizeTimeoutMs(int timeoutMs)
{
    return timeoutMs > 0 ? timeoutMs : 0;
}

static int normalizeMaxAttempts(int maxAttempts)
{
    return maxAttempts >= 1 ? maxAttempts : 1;
}

static int normalizeRetryDelayMs(int retryDelayMs)
{
    return retryDelayMs >= 0 ? retryDelayMs : 0;
}

DownloadTask::DownloadTask(const QString& url, const QString& saveDir, const QVariant& userData)
    : QObject(nullptr), QRunnable(),
    m_url(url), m_saveDir(saveDir), m_userData(userData), m_cancelled(false),
    m_timeoutMs(0), m_maxAttempts(1), m_retryDelayMs(0)
{
    QString filename = extractFilenameFromUrlImpl(url);
    m_filePath = QDir(m_saveDir).filePath(filename);
    QDir().mkpath(m_saveDir);
    
    qInfo() << "创建下载任务 - URL:" << url << "保存路径:" << m_filePath << "用户数据:" << userData;
}

DownloadTask::~DownloadTask()
{
}

void DownloadTask::run()
{
    qInfo() << "开始执行下载任务 - URL:" << m_url << "用户数据:" << m_userData;

    auto finishRun = [&]() {
        qInfo() << "下载任务执行结束 - 用户数据:" << m_userData;
    };
    
    if (m_cancelled.load(std::memory_order_relaxed)) {
        qWarning() << "下载任务已被取消，用户数据:" << m_userData;
        emit downloadFinished(false, "Cancelled", m_userData);
        finishRun();
        return;
    }

    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qCritical() << "无法打开文件进行写入:" << m_filePath;
        emit downloadFinished(false, "Cannot open file", m_userData);
        finishRun();
        return;
    }

    qInfo() << "文件已打开，开始下载到:" << m_filePath;

    auto url = QUrl(m_url);
    QNetworkRequest request(url);
    // 设置SSL配置以绕过SSL验证
    QSslConfiguration sslConfig = request.sslConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(sslConfig);

    QNetworkAccessManager localManager;
    QNetworkAccessManager* manager = &localManager;
    QNetworkReply* reply = nullptr;
#ifdef CORE_REGRESSION_TESTS
    if (m_testReplyFactory) {
        reply = m_testReplyFactory(request);
    } else if (m_testNetworkAccessManager) {
        manager = m_testNetworkAccessManager;
    }
#endif

    if (reply == nullptr) {
        reply = manager->get(request);
    }

    if (reply == nullptr) {
        qCritical() << "无法创建网络回复对象 - 用户数据:" << m_userData;
        file.close();
        emit downloadFinished(false, "Cannot create network reply", m_userData);
        finishRun();
        return;
    }
    
    // 连接SSL错误处理，忽略SSL错误
    QObject::connect(reply, &QNetworkReply::errorOccurred,
        [reply](QNetworkReply::NetworkError error) {
            if (error == QNetworkReply::SslHandshakeFailedError) {
                qWarning() << "SSL握手失败，尝试忽略错误:" << reply->errorString();
                reply->ignoreSslErrors();
            }
        });

    QObject::connect(reply, &QNetworkReply::downloadProgress, [&](qint64 rec, qint64 total) {
        if (total > 0) {
            double progress = (static_cast<double>(rec) / total) * 100.0;
            qDebug() << "下载进度 - 用户数据:" << m_userData << "已下载:" << rec << "/" << total
                     << "字节 (" << QString::number(progress, 'f', 1) << "%)";
        }
        emit progressChanged(rec, total, m_userData);
        });

    QObject::connect(reply, &QNetworkReply::readyRead, [&]() {
        if (!m_cancelled.load(std::memory_order_relaxed)) {
            QByteArray data = reply->readAll();
            file.write(data);
            qDebug() << "写入" << data.size() << "字节数据到文件";
        }
        });

    QObject::connect(reply, &QNetworkReply::finished, [&]() {
        bool success = (reply->error() == QNetworkReply::NoError && !m_cancelled.load(std::memory_order_relaxed));
        QString msg = success ? m_filePath : reply->errorString();
        
        if (success) {
            qInfo() << "下载任务成功完成 - 用户数据:" << m_userData << "文件大小:" << file.size() << "字节";
        } else {
            qWarning() << "下载任务失败 - 用户数据:" << m_userData << "错误:" << reply->errorString();
        }
        
        if (reply->parent() != nullptr) {
            reply->deleteLater();
        }
        file.close();
        emit downloadFinished(success, msg, m_userData);
        });

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer cancelTimer;
    QObject::connect(&cancelTimer, &QTimer::timeout, [&]() {
        if (m_cancelled.load(std::memory_order_relaxed) && !reply->isFinished()) {
            reply->abort();
        }
    });
    cancelTimer.start(10);
    if (m_cancelled.load(std::memory_order_relaxed) && !reply->isFinished()) {
        reply->abort();
    }
    loop.exec();

    if (reply->parent() == nullptr) {
        delete reply;
    }

    finishRun();
}

void DownloadTask::cancel()
{
    qInfo() << "取消下载任务 - 用户数据:" << m_userData;
    m_cancelled.store(true, std::memory_order_relaxed);
}

void DownloadTask::setTimeoutMs(int timeoutMs)
{
    m_timeoutMs = normalizeTimeoutMs(timeoutMs);
}

void DownloadTask::setMaxAttempts(int maxAttempts)
{
    m_maxAttempts = normalizeMaxAttempts(maxAttempts);
}

void DownloadTask::setRetryDelayMs(int retryDelayMs)
{
    m_retryDelayMs = normalizeRetryDelayMs(retryDelayMs);
}

#ifdef CORE_REGRESSION_TESTS
void DownloadTask::setTestNetworkAccessManager(QNetworkAccessManager* networkAccessManager)
{
    m_testNetworkAccessManager = networkAccessManager;
}

void DownloadTask::clearTestNetworkAccessManager()
{
    m_testNetworkAccessManager = nullptr;
}

void DownloadTask::setTestReplyFactory(const std::function<QNetworkReply*(const QNetworkRequest&)>& replyFactory)
{
    m_testReplyFactory = replyFactory;
}

void DownloadTask::clearTestReplyFactory()
{
    m_testReplyFactory = {};
}
#endif
