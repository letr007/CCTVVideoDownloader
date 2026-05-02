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

#ifdef CORE_REGRESSION_TESTS
namespace {
using DownloadTaskTestFileWriteHook = std::function<qint64(QFile&, const QByteArray&)>;
using DownloadTaskTestRenameHook = std::function<bool(const QString&, const QString&)>;

DownloadTaskTestFileWriteHook& downloadTaskTestFileWriteHook()
{
    static DownloadTaskTestFileWriteHook hook;
    return hook;
}

DownloadTaskTestRenameHook& downloadTaskTestRenameHook()
{
    static DownloadTaskTestRenameHook hook;
    return hook;
}
}

void setDownloadTaskTestFileWriteHook(const std::function<qint64(QFile&, const QByteArray&)>& hook)
{
    downloadTaskTestFileWriteHook() = hook;
}

void clearDownloadTaskTestFileWriteHook()
{
    downloadTaskTestFileWriteHook() = {};
}

void setDownloadTaskTestRenameHook(const std::function<bool(const QString&, const QString&)>& hook)
{
    downloadTaskTestRenameHook() = hook;
}

void clearDownloadTaskTestRenameHook()
{
    downloadTaskTestRenameHook() = {};
}

static qint64 writeDownloadTaskData(QFile& file, const QByteArray& data)
{
    auto& hook = downloadTaskTestFileWriteHook();
    return hook ? hook(file, data) : file.write(data);
}

static bool renameDownloadTaskPath(const QString& from, const QString& to)
{
    auto& hook = downloadTaskTestRenameHook();
    return hook ? hook(from, to) : QFile::rename(from, to);
}
#else
static qint64 writeDownloadTaskData(QFile& file, const QByteArray& data)
{
    return file.write(data);
}

static bool renameDownloadTaskPath(const QString& from, const QString& to)
{
    return QFile::rename(from, to);
}
#endif

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

    const QString partPath = m_filePath + QStringLiteral(".part");
    if (QFile::exists(partPath)) {
        if (!QFile::remove(partPath)) {
            qWarning() << "移除旧的临时文件失败:" << partPath;
        } else {
            qInfo() << "已移除旧的临时文件，准备开始第一次下载尝试:" << partPath;
        }
    }
    QFile file(partPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCritical() << "无法打开临时文件进行写入:" << partPath;
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
    qint64 writtenBytes = 0;
    qint64 expectedBytes = 0;

    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        writtenBytes = 0;
        expectedBytes = 0;

        if (attempt > 1) {
            if (file.isOpen()) {
                file.close();
            }

            if (QFile::exists(partPath) && !QFile::remove(partPath)) {
                qWarning() << "重试前无法移除旧临时文件:" << partPath;
            }

            if (!waitForRetryDelay(retryDelayMs)) {
                emit downloadFinished(false, "Cancelled", m_userData);
                finishRun();
                return;
            }
        }

        if (m_cancelled.load(std::memory_order_relaxed)) {
            file.close();
            if (QFile::exists(partPath) && !QFile::remove(partPath)) {
                qWarning() << "移除临时文件失败:" << partPath;
            }
            emit downloadFinished(false, "Cancelled", m_userData);
            finishRun();
            return;
        }

        if (!file.isOpen() && !file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qCritical() << "重试时无法打开临时文件进行写入:" << partPath;
            if (QFile::exists(partPath) && !QFile::remove(partPath)) {
                qWarning() << "移除临时文件失败:" << partPath;
            }
            emit downloadFinished(false, "Cannot open file", m_userData);
            finishRun();
            return;
        }

        qInfo() << "临时文件已打开，开始下载到:" << partPath << "尝试:" << attempt << "/" << maxAttempts;

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
            if (QFile::exists(partPath) && !QFile::remove(partPath)) {
                qWarning() << "移除临时文件失败:" << partPath;
            }
            emit downloadFinished(false, "Cannot create network reply", m_userData);
            finishRun();
            return;
        }

        {
            qint64 contentLength = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
            if (contentLength > 0) {
                expectedBytes = contentLength;
            }
        }

        bool timedOut = false;
        bool attemptSuccess = false;
        QString attemptMessage;
        QNetworkReply::NetworkError attemptError = QNetworkReply::NoError;
        const QString timeoutMessage = QStringLiteral("Timed out after %1 ms").arg(timeoutMs);
        bool writeFailed = false;
        QString writeFailureMessage;
        qint64 lastBytesReceived = 0;
        qint64 lastBytesTotal = -1;

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

            if (lastBytesTotal > 0 && writtenBytes >= lastBytesTotal) {
                return true;
            }

            return false;
        };
        auto resetInactivityTimer = [&]() {
            if (timeoutMs > 0
                && !timedOut
                && !m_cancelled.load(std::memory_order_relaxed)
                && !reply->isFinished()) {
                const int activeTimeoutMs = hasFullyReceivedPayload()
                    ? std::max(timeoutMs, 1000)
                    : timeoutMs;
                inactivityTimer.start(activeTimeoutMs);
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
                expectedBytes = total;
                double progress = (static_cast<double>(rec) / total) * 100.0;
                qDebug() << "下载进度 - 用户数据:" << m_userData << "已下载:" << rec << "/" << total
                         << "字节 (" << QString::number(progress, 'f', 1) << "%)";
            }
            emit progressChanged(rec, total, m_userData);
        });

        QObject::connect(reply, &QNetworkReply::readyRead, [&]() {
            if (!m_cancelled.load(std::memory_order_relaxed)) {
                const QByteArray data = reply->readAll();
                qint64 written = writeDownloadTaskData(file, data);
                if (written < 0 || written != data.size()) {
                    writeFailed = true;
                    if (maxAttempts > 1) {
                        writeFailureMessage = QStringLiteral("Download failed [code=write_failed; attempts=%1/%2]: Wrote %3 of %4 bytes to temp file %5")
                            .arg(attempt).arg(maxAttempts).arg(written).arg(data.size()).arg(partPath);
                    } else {
                        writeFailureMessage = QStringLiteral("Download failed [code=write_failed; attempts=1/1]: Wrote %1 of %2 bytes to temp file %3")
                            .arg(written).arg(data.size()).arg(partPath);
                    }
                    qCritical() << "写入临时文件失败或短写:" << partPath << "期望写入:" << data.size() << "实际写入:" << written;
                    if (!reply->isFinished()) {
                        reply->abort();
                    }
                    return;
                }
                writtenBytes += written;
                qDebug() << "写入" << data.size() << "字节数据到临时文件，实际写入:" << written;
                const auto contentLength = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
                if (contentLength > 0 && lastBytesTotal <= 0) {
                    lastBytesTotal = contentLength;
                }
                resetInactivityTimer();
            }
        });

        QObject::connect(reply, &QNetworkReply::finished, [&]() {
            inactivityTimer.stop();
            const bool cancelled = m_cancelled.load(std::memory_order_relaxed);
            attemptError = reply->error();
            attemptSuccess = (reply->error() == QNetworkReply::NoError && !cancelled && !timedOut && !writeFailed);
            attemptMessage = attemptSuccess ? m_filePath : reply->errorString();

            if (timedOut) {
                attemptMessage = maxAttempts > 1
                    ? QStringLiteral("Download failed [code=timeout; attempts=%1/%2]: %3").arg(attempt).arg(maxAttempts).arg(timeoutMessage)
                    : QStringLiteral("Download failed [code=timeout; attempts=1/1]: %1").arg(timeoutMessage);
            } else if (!cancelled && writeFailed) {
                attemptMessage = writeFailureMessage;
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

        file.close();

        if (attemptSuccess) {
            if (writtenBytes <= 0) {
                attemptSuccess = false;
                if (maxAttempts > 1) {
                    attemptMessage = QStringLiteral("Download failed [code=integrity; attempts=%1/%2]: Downloaded zero bytes to %3")
                        .arg(attempt).arg(maxAttempts).arg(partPath);
                } else {
                    attemptMessage = QStringLiteral("Download failed [code=integrity; attempts=1/1]: Downloaded zero bytes to %1")
                        .arg(partPath);
                }
            } else if (expectedBytes > 0 && writtenBytes != expectedBytes) {
                attemptSuccess = false;
                if (maxAttempts > 1) {
                    attemptMessage = QStringLiteral("Download failed [code=integrity; attempts=%1/%2]: Wrote %3 bytes to %4, expected %5 bytes")
                        .arg(attempt).arg(maxAttempts).arg(writtenBytes).arg(partPath).arg(expectedBytes);
                } else {
                    attemptMessage = QStringLiteral("Download failed [code=integrity; attempts=1/1]: Wrote %1 bytes to %2, expected %3 bytes")
                        .arg(writtenBytes).arg(partPath).arg(expectedBytes);
                }
            }
        }

        if (attemptSuccess) {
            const QString backupPath = m_filePath + QStringLiteral(".publish_backup");
            const bool hadExistingFinal = QFile::exists(m_filePath);
            bool originalMovedToBackup = false;

            if (hadExistingFinal) {
                if (QFile::exists(backupPath) && !QFile::remove(backupPath)) {
                    attemptSuccess = false;
                    qCritical() << "清理发布备份文件失败，无法发布最终文件:" << backupPath;
                } else if (!renameDownloadTaskPath(m_filePath, backupPath)) {
                    attemptSuccess = false;
                    qCritical() << "备份现有最终文件失败，无法发布新文件:" << m_filePath << "->" << backupPath;
                } else {
                    originalMovedToBackup = true;
                }
            }

            if (!attemptSuccess) {
                if (maxAttempts > 1) {
                    attemptMessage = QStringLiteral("Download failed [code=rename_failed; attempts=%1/%2]: Could not publish %3 via backup %4")
                        .arg(attempt).arg(maxAttempts).arg(m_filePath).arg(backupPath);
                } else {
                    attemptMessage = QStringLiteral("Download failed [code=rename_failed; attempts=1/1]: Could not publish %1 via backup %2")
                        .arg(m_filePath).arg(backupPath);
                }
            } else if (!renameDownloadTaskPath(partPath, m_filePath)) {
                attemptSuccess = false;
                if (maxAttempts > 1) {
                    attemptMessage = QStringLiteral("Download failed [code=rename_failed; attempts=%1/%2]: Could not publish %3 from %4")
                        .arg(attempt).arg(maxAttempts).arg(m_filePath).arg(partPath);
                } else {
                    attemptMessage = QStringLiteral("Download failed [code=rename_failed; attempts=1/1]: Could not publish %1 from %2")
                        .arg(m_filePath).arg(partPath);
                }

                qCritical() << "发布最终文件失败:" << partPath << "->" << m_filePath;

                if (originalMovedToBackup && !renameDownloadTaskPath(backupPath, m_filePath)) {
                    qCritical() << "恢复原始最终文件失败:" << backupPath << "->" << m_filePath;
                }
            } else if (originalMovedToBackup && QFile::exists(backupPath) && !QFile::remove(backupPath)) {
                qWarning() << "清理发布备份文件失败:" << backupPath;
            }
        }

        finalSuccess = attemptSuccess;
        finalMessage = attemptMessage;

        if (attemptSuccess) {
            break;
        }

        const bool cancelled = m_cancelled.load(std::memory_order_relaxed);
        const bool canRetry = !cancelled
            && attempt < maxAttempts
            && (timedOut || writeFailed || attemptError != QNetworkReply::OperationCanceledError);

        if (!canRetry) {
            break;
        }
    }

    file.close();

    if (finalSuccess) {
        qInfo() << "下载任务成功完成 - 用户数据:" << m_userData << "文件大小:" << QFileInfo(m_filePath).size() << "字节";
    } else {
        qWarning() << "下载任务失败 - 用户数据:" << m_userData << "错误:" << finalMessage;
        if (QFile::exists(partPath) && !QFile::remove(partPath)) {
            qWarning() << "下载失败后清理临时文件失败:" << partPath;
        }
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
