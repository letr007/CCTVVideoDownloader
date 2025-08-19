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

static QString extractFilenameFromUrlImpl(const QString& url) {
    QUrl qurl(url);
    QString path = qurl.path();
    QString filename = path.mid(path.lastIndexOf('/') + 1);
    if (filename.isEmpty()) {
        filename = "download_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".dat";
    }
    return filename;
}

DownloadTask::DownloadTask(const QString& url, const QString& saveDir, const QVariant& userData)
    : QObject(nullptr), QRunnable(),
    m_url(url), m_saveDir(saveDir), m_userData(userData), m_cancelled(false)
{
    QString filename = extractFilenameFromUrlImpl(url);
    m_filePath = QDir(m_saveDir).filePath(filename);
    QDir().mkpath(m_saveDir);
}

DownloadTask::~DownloadTask()
{
}

void DownloadTask::run()
{
    if (m_cancelled) {
        emit downloadFinished(false, "Cancelled", m_userData);
        return;
    }

    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit downloadFinished(false, "Cannot open file", m_userData);
        return;
    }

    QNetworkAccessManager manager;
    auto url = QUrl(m_url);
    QNetworkRequest request(url);
    QNetworkReply* reply = manager.get(request);

    QObject::connect(reply, &QNetworkReply::downloadProgress, [&](qint64 rec, qint64 total) {
        emit progressChanged(rec, total, m_userData);
        });

    QObject::connect(reply, &QNetworkReply::readyRead, [&]() {
        if (!m_cancelled) {
            file.write(reply->readAll());
        }
        });

    QObject::connect(reply, &QNetworkReply::finished, [&]() {
        bool success = (reply->error() == QNetworkReply::NoError && !m_cancelled);
        QString msg = success ? m_filePath : reply->errorString();
        reply->deleteLater();
        file.close();
        emit downloadFinished(success, msg, m_userData);
        });

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
}

void DownloadTask::cancel()
{
    m_cancelled = true;
}
