#include "fake_networkaccessmanager.h"

#include <QMutexLocker>
#include <QTimer>

FakeNetworkAccessManager::FakeNetworkAccessManager(QObject* parent)
    : QNetworkAccessManager(parent)
{
}

void FakeNetworkAccessManager::queueSuccess(const QUrl& url, const QByteArray& body, int finishDelayMs, qint64 bytesTotal)
{
    QMutexLocker locker(&m_mutex);
    QueuedReply queuedReply;
    queuedReply.expectedUrl = url;
    queuedReply.response.body = body;
    queuedReply.response.finishDelayMs = finishDelayMs;
    queuedReply.response.bytesTotal = bytesTotal;
    m_queue.append(queuedReply);
}

void FakeNetworkAccessManager::queueError(const QUrl& url,
                                          QNetworkReply::NetworkError error,
                                          const QString& errorString,
                                          int finishDelayMs)
{
    QMutexLocker locker(&m_mutex);
    QueuedReply queuedReply;
    queuedReply.expectedUrl = url;
    queuedReply.response.error = error;
    queuedReply.response.errorString = errorString;
    queuedReply.response.finishDelayMs = finishDelayMs;
    m_queue.append(queuedReply);
}

int FakeNetworkAccessManager::queuedReplyCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_queue.size();
}

int FakeNetworkAccessManager::requestCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_requestedUrls.size();
}

int FakeNetworkAccessManager::unexpectedRequestCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_unexpectedFailures.size();
}

QList<QUrl> FakeNetworkAccessManager::requestedUrls() const
{
    QMutexLocker locker(&m_mutex);
    return m_requestedUrls;
}

QStringList FakeNetworkAccessManager::unexpectedFailures() const
{
    QMutexLocker locker(&m_mutex);
    return m_unexpectedFailures;
}

FakeNetworkReply* FakeNetworkAccessManager::lastReply() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastReply;
}

QNetworkReply* FakeNetworkAccessManager::createRequest(Operation operation, const QNetworkRequest& request, QIODevice* outgoingData)
{
    Q_UNUSED(outgoingData);

    return createReplyForRequest(operation, request, this);
}

QNetworkReply* FakeNetworkAccessManager::createReplyForRequest(Operation operation, const QNetworkRequest& request, QObject* parent)
{
    QMutexLocker locker(&m_mutex);

    m_requestedUrls.append(request.url());

    if (m_queue.isEmpty()) {
        return createUnexpectedReply(operation, request, QStringLiteral("No queued fake reply available"), parent);
    }

    const QueuedReply nextReply = m_queue.front();
    if (nextReply.operation != operation || nextReply.expectedUrl != request.url()) {
        QString reason = QStringLiteral("Unexpected request %1 %2; next queued fake expects %3 %4")
                             .arg(QString::number(operation),
                                  request.url().toString(),
                                  QString::number(nextReply.operation),
                                  nextReply.expectedUrl.toString());
        return createUnexpectedReply(operation, request, reason, parent);
    }

    m_queue.pop_front();
    auto* reply = new FakeNetworkReply(request, operation, nextReply.response, parent);
    m_lastReply = reply;
    QTimer::singleShot(0, reply, &FakeNetworkReply::start);
    return reply;
}

FakeNetworkReply* FakeNetworkAccessManager::createUnexpectedReply(Operation operation, const QNetworkRequest& request, const QString& reason, QObject* parent)
{
    m_unexpectedFailures.append(reason);

    FakeNetworkReply::ResponseSpec response;
    response.error = QNetworkReply::ProtocolFailure;
    response.errorString = reason;

    auto* reply = new FakeNetworkReply(request, operation, response, parent);
    m_lastReply = reply;
    QTimer::singleShot(0, reply, &FakeNetworkReply::start);
    return reply;
}
