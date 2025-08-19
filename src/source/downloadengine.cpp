#include "../head/downloadengine.h"
#include "../head/downloadtask.h"
#include <QDebug>

inline size_t qHash(const QVariant& var, size_t seed = 0)
{
    if (!var.isValid()) return 0;
    return qHash(var.toString(), seed);
}

DownloadEngine::DownloadEngine(QObject* parent)
    : QObject(parent)
{
    m_threadPool.setMaxThreadCount(2); // 默认2线程并发
    m_threadPool.setExpiryTimeout(0);
}

DownloadEngine::~DownloadEngine()
{
    cancelAll();
}

void DownloadEngine::download(const QString& url, const QString& saveDir, const QVariant& userData)
{
    QMutexLocker locker(&m_mutex);
    if (m_activeDownloads.contains(userData)) {
        qWarning() << "Download with same userData exists!";
        return;
    }

    auto* task = new DownloadTask(url, saveDir, userData);
    task->setAutoDelete(false);

    connect(task, &DownloadTask::progressChanged, this, &DownloadEngine::onDownloadProgress, Qt::QueuedConnection);
    connect(task, &DownloadTask::downloadFinished, this, &DownloadEngine::onDownloadFinished, Qt::QueuedConnection);

    m_activeDownloads.insert(userData, task);
    m_threadPool.start(task);
}

void DownloadEngine::cancelDownload(const QVariant& userData)
{
    QMutexLocker locker(&m_mutex);
    if (m_activeDownloads.contains(userData)) {
        m_activeDownloads[userData]->cancel();
    }
}

void DownloadEngine::cancelAll()
{
    QMutexLocker locker(&m_mutex);
    for (auto* task : m_activeDownloads) {
        task->cancel();
    }
}

int DownloadEngine::activeDownloads() const
{
    QMutexLocker locker(&m_mutex);
    return m_activeDownloads.size();
}

int DownloadEngine::maxThreadCount() const
{
    return m_threadPool.maxThreadCount();
}

void DownloadEngine::setMaxThreadCount(int count)
{
    m_threadPool.setMaxThreadCount(count);
}

void DownloadEngine::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal, const QVariant& userData)
{
    emit downloadProgress(bytesReceived, bytesTotal, userData);
}

void DownloadEngine::onDownloadFinished(bool success, const QString& errorString, const QVariant& userData)
{
    QMutexLocker locker(&m_mutex);
    // 取出task并释放
    auto task = m_activeDownloads.take(userData);
    locker.unlock();
    delete task;
    m_activeDownloads.remove(userData);

    emit downloadFinished(success, errorString, userData);

    QMutexLocker locker2(&m_mutex);
    if (m_activeDownloads.isEmpty()) {
        locker2.unlock();
        emit allDownloadFinished();
    }
}

void DownloadEngine::waitForAllFinished()
{
    m_threadPool.waitForDone();
}