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
    
    qInfo() << "创建下载任务 - URL:" << url << "保存路径:" << m_filePath << "用户数据:" << userData;
}

DownloadTask::~DownloadTask()
{
}

void DownloadTask::run()
{
    qInfo() << "开始执行下载任务 - URL:" << m_url << "用户数据:" << m_userData;
    
    if (m_cancelled) {
        qWarning() << "下载任务已被取消，用户数据:" << m_userData;
        emit downloadFinished(false, "Cancelled", m_userData);
        return;
    }

    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qCritical() << "无法打开文件进行写入:" << m_filePath;
        emit downloadFinished(false, "Cannot open file", m_userData);
        return;
    }

    qInfo() << "文件已打开，开始下载到:" << m_filePath;

    QNetworkAccessManager manager;
    auto url = QUrl(m_url);
    QNetworkRequest request(url);
    QNetworkReply* reply = manager.get(request);

    QObject::connect(reply, &QNetworkReply::downloadProgress, [&](qint64 rec, qint64 total) {
        if (total > 0) {
            double progress = (static_cast<double>(rec) / total) * 100.0;
            qDebug() << "下载进度 - 用户数据:" << m_userData << "已下载:" << rec << "/" << total
                     << "字节 (" << QString::number(progress, 'f', 1) << "%)";
        }
        emit progressChanged(rec, total, m_userData);
        });

    QObject::connect(reply, &QNetworkReply::readyRead, [&]() {
        if (!m_cancelled) {
            QByteArray data = reply->readAll();
            file.write(data);
            qDebug() << "写入" << data.size() << "字节数据到文件";
        }
        });

    QObject::connect(reply, &QNetworkReply::finished, [&]() {
        bool success = (reply->error() == QNetworkReply::NoError && !m_cancelled);
        QString msg = success ? m_filePath : reply->errorString();
        
        if (success) {
            qInfo() << "下载任务成功完成 - 用户数据:" << m_userData << "文件大小:" << file.size() << "字节";
        } else {
            qWarning() << "下载任务失败 - 用户数据:" << m_userData << "错误:" << reply->errorString();
        }
        
        reply->deleteLater();
        file.close();
        emit downloadFinished(success, msg, m_userData);
        });

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    
    qInfo() << "下载任务执行结束 - 用户数据:" << m_userData;
}

void DownloadTask::cancel()
{
    qInfo() << "取消下载任务 - 用户数据:" << m_userData;
    m_cancelled = true;
}
