#pragma once

#include <QList>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>

#include "fake_networkreply.h"

class FakeNetworkAccessManager : public QNetworkAccessManager
{
    Q_OBJECT

public:
    struct QueuedReply {
        QNetworkAccessManager::Operation operation = QNetworkAccessManager::GetOperation;
        QUrl expectedUrl;
        FakeNetworkReply::ResponseSpec response;
    };

    explicit FakeNetworkAccessManager(QObject* parent = nullptr);

    void queueSuccess(const QUrl& url, const QByteArray& body, int finishDelayMs = 0, qint64 bytesTotal = -2);
    void queueError(const QUrl& url,
                    QNetworkReply::NetworkError error,
                    const QString& errorString,
                    int finishDelayMs = 0);

    int queuedReplyCount() const;
    int requestCount() const;
    int unexpectedRequestCount() const;
    QList<QUrl> requestedUrls() const;
    QStringList unexpectedFailures() const;
    FakeNetworkReply* lastReply() const;
    QNetworkReply* createReplyForRequest(Operation operation, const QNetworkRequest& request, QObject* parent = nullptr);

protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request, QIODevice* outgoingData) override;

private:
    FakeNetworkReply* createUnexpectedReply(Operation operation, const QNetworkRequest& request, const QString& reason, QObject* parent = nullptr);

    QList<QueuedReply> m_queue;
    QList<QUrl> m_requestedUrls;
    QStringList m_unexpectedFailures;
    QPointer<FakeNetworkReply> m_lastReply;
    mutable QMutex m_mutex;
};
