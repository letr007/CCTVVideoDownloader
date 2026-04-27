#pragma once
#include <QObject>
#include <QRunnable>
#include <QVariant>
#include <QString>
#include <QPointer>
#include <atomic>

#ifdef CORE_REGRESSION_TESTS
#include <functional>
#endif

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

class DownloadTask : public QObject, public QRunnable
{
    Q_OBJECT

#ifdef CORE_REGRESSION_TESTS
    friend class DownloadTaskTestAdapter;
    friend class DownloadEngine;
#endif

public:
    DownloadTask(const QString& url, const QString& saveDir, const QVariant& userData);
    ~DownloadTask();

    void run() override;
    void cancel();
    void setAutoDelete(bool autoDelete) { QRunnable::setAutoDelete(autoDelete); }

signals:
    void progressChanged(qint64 bytesReceived, qint64 bytesTotal, const QVariant& userData);
    void downloadFinished(bool success, const QString& errorString, const QVariant& userData);
    void runCompleted();

private:
#ifdef CORE_REGRESSION_TESTS
    void setTestNetworkAccessManager(QNetworkAccessManager* networkAccessManager);
    void clearTestNetworkAccessManager();
    void setTestReplyFactory(const std::function<QNetworkReply*(const QNetworkRequest&)>& replyFactory);
    void clearTestReplyFactory();
#endif

    QString m_url;
    QString m_saveDir;
    QString m_filePath;
    QVariant m_userData;
    std::atomic_bool m_cancelled;

#ifdef CORE_REGRESSION_TESTS
    QPointer<QNetworkAccessManager> m_testNetworkAccessManager;
    std::function<QNetworkReply*(const QNetworkRequest&)> m_testReplyFactory;
#endif
};
