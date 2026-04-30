#include "../head/downloadtask.h"
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
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

    const int maxAttempts = m_maxAttempts;
    const int retryDelayMs = m_retryDelayMs;
    const int timeoutMs = m_timeoutMs;
    
    if (m_cancelled.load(std::memory_order_relaxed)) {
        qWarning() << "下载任务已被取消，用户数据:" << m_userData;
        emit downloadFinished(false, "Cancelled", m_userData);
        finishRun();
        return;
    }

    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCritical() << "无法打开文件进行写入:" << m_filePath;
        emit downloadFinished(false, "Cannot open file", m_userData);
        finishRun();
        return;
    }

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
        manager = nullptr;
    } else if (m_testNetworkAccessManager) {
        manager = m_testNetworkAccessManager;
    }
#endif

    auto waitForRetryDelay = [&](int delayMs) {
        if (delayMs <= 0) {
            return !m_cancelled.load(std::memory_order_relaxed);
        }

        QEventLoop delayLoop;
        QTimer delayTimer;
        QTimer cancelTimer;
        delayTimer.setSingleShot(true);

        QObject::connect(&delayTimer, &QTimer::timeout, &delayLoop, &QEventLoop::quit);
        QObject::connect(&cancelTimer, &QTimer::timeout, [&]() {
            if (m_cancelled.load(std::memory_order_relaxed)) {
                delayLoop.quit();
            }
        });

        delayTimer.start(delayMs);
        cancelTimer.start(10);
        if (!m_cancelled.load(std::memory_order_relaxed)) {
            delayLoop.exec();
        }
        cancelTimer.stop();

        return !m_cancelled.load(std::memory_order_relaxed);
    };

    bool finalSuccess = false;
    QString finalMessage;

    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        if (attempt > 1) {
            if (!file.resize(0)) {
                file.close();
                qCritical() << "重试前无法重置文件:" << m_filePath;
                emit downloadFinished(false, "Cannot open file", m_userData);
                finishRun();
                return;
            }

            if (!waitForRetryDelay(retryDelayMs)) {
                file.close();
                emit downloadFinished(false, "Cancelled", m_userData);
                finishRun();
                return;
            }
        }

        if (m_cancelled.load(std::memory_order_relaxed)) {
            file.close();
            emit downloadFinished(false, "Cancelled", m_userData);
            finishRun();
            return;
        }

        if (!file.isOpen() && !file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qCritical() << "无法打开文件进行写入:" << m_filePath;
            emit downloadFinished(false, "Cannot open file", m_userData);
            finishRun();
            return;
        }

        if (attempt > 1 && !file.seek(0)) {
            file.close();
            qCritical() << "重试前无法定位文件开头:" << m_filePath;
            emit downloadFinished(false, "Cannot open file", m_userData);
            finishRun();
            return;
        }

        qInfo() << "文件已打开，开始下载到:" << m_filePath << "尝试:" << attempt << "/" << maxAttempts;

#ifdef CORE_REGRESSION_TESTS
        if (m_testReplyFactory) {
            reply = m_testReplyFactory(request);
        } else if (manager != nullptr) {
            reply = manager->get(request);
        } else {
            reply = nullptr;
        }
#else
        reply = manager->get(request);
#endif

        if (reply == nullptr) {
            qCritical() << "无法创建网络回复对象 - 用户数据:" << m_userData;
            file.close();
            emit downloadFinished(false, "Cannot create network reply", m_userData);
            finishRun();
            return;
        }

        bool timedOut = false;
        bool attemptSuccess = false;
        QString attemptMessage;
        QNetworkReply::NetworkError attemptError = QNetworkReply::NoError;
        const QString timeoutMessage = QStringLiteral("Timed out after %1 ms").arg(timeoutMs);
        qint64 lastBytesReceived = 0;
        qint64 lastBytesTotal = -1;
        qint64 bytesWritten = 0;

        QObject::connect(reply, &QNetworkReply::errorOccurred,
            [reply](QNetworkReply::NetworkError error) {
                if (error == QNetworkReply::SslHandshakeFailedError) {
                    qWarning() << "SSL握手失败，尝试忽略错误:" << reply->errorString();
                    reply->ignoreSslErrors();
                }
            });

        QTimer inactivityTimer;
        inactivityTimer.setSingleShot(true);
        auto hasFullyReceivedPayload = [&]() {
            if (lastBytesTotal > 0 && lastBytesReceived >= lastBytesTotal) {
                return true;
            }

            if (lastBytesTotal > 0 && bytesWritten >= lastBytesTotal) {
                return true;
            }

            return false;
        };
        auto resetInactivityTimer = [&]() {
            if (timeoutMs > 0
                && !timedOut
                && !m_cancelled.load(std::memory_order_relaxed)
                && !reply->isFinished()
                && !hasFullyReceivedPayload()) {
                inactivityTimer.start(timeoutMs);
            } else {
                inactivityTimer.stop();
            }
        };

        QObject::connect(&inactivityTimer, &QTimer::timeout, [&]() {
            if (timeoutMs <= 0 || timedOut || m_cancelled.load(std::memory_order_relaxed) || reply->isFinished()) {
                return;
            }

            timedOut = true;
            qWarning() << "下载任务超时 - 用户数据:" << m_userData << "超时毫秒:" << timeoutMs;
            reply->abort();
        });

        QObject::connect(reply, &QNetworkReply::downloadProgress, [&](qint64 rec, qint64 total) {
            lastBytesReceived = rec;
            lastBytesTotal = total;
            resetInactivityTimer();
            if (total > 0) {
                double progress = (static_cast<double>(rec) / total) * 100.0;
                qDebug() << "下载进度 - 用户数据:" << m_userData << "已下载:" << rec << "/" << total
                         << "字节 (" << QString::number(progress, 'f', 1) << "%)";
            }
            emit progressChanged(rec, total, m_userData);
        });

        QObject::connect(reply, &QNetworkReply::readyRead, [&]() {
            if (!m_cancelled.load(std::memory_order_relaxed)) {
                const QByteArray data = reply->readAll();
                file.write(data);
                bytesWritten += data.size();
                const auto contentLength = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
                if (contentLength > 0 && lastBytesTotal <= 0) {
                    lastBytesTotal = contentLength;
                }
                resetInactivityTimer();
                qDebug() << "写入" << data.size() << "字节数据到文件";
            }
        });

        QObject::connect(reply, &QNetworkReply::finished, [&]() {
            inactivityTimer.stop();
            const bool cancelled = m_cancelled.load(std::memory_order_relaxed);
            attemptError = reply->error();
            attemptSuccess = (reply->error() == QNetworkReply::NoError && !cancelled && !timedOut);
            attemptMessage = attemptSuccess ? m_filePath : reply->errorString();

            if (timedOut) {
                attemptMessage = maxAttempts > 1
                    ? QStringLiteral("Download failed [code=timeout; attempts=%1/%2]: %3").arg(attempt).arg(maxAttempts).arg(timeoutMessage)
                    : QStringLiteral("Download failed [code=timeout; attempts=1/1]: %1").arg(timeoutMessage);
            } else if (!attemptSuccess && !cancelled && attemptError != QNetworkReply::OperationCanceledError && maxAttempts > 1) {
                attemptMessage = QStringLiteral("Download failed [code=network_error; attempts=%1/%2]: %3")
                    .arg(attempt)
                    .arg(maxAttempts)
                    .arg(reply->errorString());
            }

            if (reply->parent() != nullptr) {
                reply->deleteLater();
            }
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
        resetInactivityTimer();
        if (m_cancelled.load(std::memory_order_relaxed) && !reply->isFinished()) {
            reply->abort();
        }
        loop.exec();
        cancelTimer.stop();

        if (reply->parent() == nullptr) {
            delete reply;
        }

        finalSuccess = attemptSuccess;
        finalMessage = attemptMessage;

        if (attemptSuccess) {
            break;
        }

        const bool cancelled = m_cancelled.load(std::memory_order_relaxed);
        const bool canRetry = !cancelled
            && attempt < maxAttempts
            && (timedOut || attemptError != QNetworkReply::OperationCanceledError);

        if (!canRetry) {
            break;
        }
    }

    file.close();

    if (finalSuccess) {
        qInfo() << "下载任务成功完成 - 用户数据:" << m_userData << "文件大小:" << QFileInfo(m_filePath).size() << "字节";
    } else {
        qWarning() << "下载任务失败 - 用户数据:" << m_userData << "错误:" << finalMessage;
    }

    emit downloadFinished(finalSuccess, finalMessage, m_userData);

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
