#pragma once
#include <QObject>
#include <QRunnable>
#include <QVariant>
#include <QString>
#include <QPointer>

class QNetworkAccessManager;

class DownloadTask : public QObject, public QRunnable
{
    Q_OBJECT

#ifdef CORE_REGRESSION_TESTS
    friend class DownloadTaskTestAdapter;
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

private:
#ifdef CORE_REGRESSION_TESTS
    void setTestNetworkAccessManager(QNetworkAccessManager* networkAccessManager);
    void clearTestNetworkAccessManager();
#endif

    QString m_url;
    QString m_saveDir;
    QString m_filePath;
    QVariant m_userData;
    bool m_cancelled;

#ifdef CORE_REGRESSION_TESTS
    QPointer<QNetworkAccessManager> m_testNetworkAccessManager;
#endif
};
