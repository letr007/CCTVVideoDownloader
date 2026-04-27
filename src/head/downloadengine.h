#pragma once
#include <QObject>
#include <QThreadPool>
#include <QVariant>
#include <QMutex>
#include <QHash>
#include <QSet>

#ifdef CORE_REGRESSION_TESTS
#include <functional>
#endif

class DownloadTask;

#ifdef CORE_REGRESSION_TESTS
class QNetworkReply;
class QNetworkRequest;
#endif

class DownloadEngine : public QObject
{
    Q_OBJECT
public:
    explicit DownloadEngine(QObject* parent = nullptr);
    ~DownloadEngine();

    void download(const QString& url, const QString& saveDir, const QVariant& userData);
    void cancelDownload(const QVariant& userData);
    void cancelAll();

    int activeDownloads() const;
    int maxThreadCount() const;
    void setMaxThreadCount(int count);

    void waitForAllFinished();

#ifdef CORE_REGRESSION_TESTS
    void setTestReplyFactory(const std::function<QNetworkReply*(const QNetworkRequest&)>& replyFactory);
    void clearTestReplyFactory();
#endif

signals:
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal, const QVariant& userData);
    void downloadFinished(bool success, const QString& errorString, const QVariant& userData);
    void allDownloadFinished();

private slots:
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal, const QVariant& userData);
    void onDownloadFinished(bool success, const QString& errorString, const QVariant& userData);

private:
    void deleteTrackedTasks();
    void deleteCompletedTasks();

    QThreadPool m_threadPool;
    QHash<QVariant, DownloadTask*> m_activeDownloads;
    QSet<DownloadTask*> m_completedTasks;
    mutable QMutex m_mutex;

#ifdef CORE_REGRESSION_TESTS
    std::function<QNetworkReply*(const QNetworkRequest&)> m_testReplyFactory;
#endif
};
