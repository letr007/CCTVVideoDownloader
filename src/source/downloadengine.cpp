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
    m_threadPool.waitForDone();
    deleteTrackedTasks();
}

void DownloadEngine::download(const QString& url, const QString& saveDir, const QVariant& userData)
{
    qInfo() << "开始下载任务 - URL:" << url << "保存目录:" << saveDir << "用户数据:" << userData;
    
    QMutexLocker locker(&m_mutex);
    if (m_activeDownloads.contains(userData)) {
        qWarning() << "下载任务已存在，用户数据:" << userData;
        return;
    }

    auto* task = new DownloadTask(url, saveDir, userData);
    task->setAutoDelete(false);
    task->setTimeoutMs(m_defaultTimeoutMs);
    task->setMaxAttempts(m_defaultMaxAttempts);
    task->setRetryDelayMs(m_defaultRetryDelayMs);

#ifdef CORE_REGRESSION_TESTS
    if (m_testReplyFactory) {
        task->setTestReplyFactory(m_testReplyFactory);
    }
#endif

    connect(task, &DownloadTask::progressChanged, this, &DownloadEngine::onDownloadProgress, Qt::QueuedConnection);
    connect(task, &DownloadTask::downloadFinished, this, &DownloadEngine::onDownloadFinished, Qt::QueuedConnection);

    m_activeDownloads.insert(userData, task);
    m_threadPool.start(task);
    
    qInfo() << "下载任务已添加到线程池，当前活跃任务数:" << m_activeDownloads.size();
}

void DownloadEngine::cancelDownload(const QVariant& userData)
{
    qInfo() << "取消下载任务，用户数据:" << userData;
    
    QMutexLocker locker(&m_mutex);
    if (m_activeDownloads.contains(userData)) {
        m_activeDownloads[userData]->cancel();
        qInfo() << "下载任务已取消";
    } else {
        qWarning() << "未找到对应的下载任务，用户数据:" << userData;
    }
}

void DownloadEngine::cancelAll()
{
    qInfo() << "取消所有下载任务";
    
    QMutexLocker locker(&m_mutex);
    int taskCount = m_activeDownloads.size();
    for (auto* task : m_activeDownloads) {
        task->cancel();
    }
    
    qInfo() << "已取消" << taskCount << "个下载任务";
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

void DownloadEngine::setDefaultTimeoutMs(int timeoutMs)
{
    m_defaultTimeoutMs = timeoutMs > 0 ? timeoutMs : 0;
}

void DownloadEngine::setDefaultMaxAttempts(int maxAttempts)
{
    m_defaultMaxAttempts = maxAttempts >= 1 ? maxAttempts : 1;
}

void DownloadEngine::setDefaultRetryDelayMs(int retryDelayMs)
{
    m_defaultRetryDelayMs = retryDelayMs >= 0 ? retryDelayMs : 0;
}

void DownloadEngine::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal, const QVariant& userData)
{
    double progress = bytesTotal > 0 ? (static_cast<double>(bytesReceived) / bytesTotal) * 100.0 : 0.0;
    qDebug() << "下载进度 - 用户数据:" << userData << "已接收:" << bytesReceived << "字节，总计:" << bytesTotal
             << "字节，进度:" << QString::number(progress, 'f', 1) << "%";
    
    emit downloadProgress(bytesReceived, bytesTotal, userData);
}

void DownloadEngine::onDownloadFinished(bool success, const QString& errorString, const QVariant& userData)
{
    qInfo() << "下载任务完成 - 用户数据:" << userData << "成功:" << success << "错误信息:" << errorString;

    bool allFinished = false;

    {
        QMutexLocker locker(&m_mutex);
        if (auto* task = m_activeDownloads.take(userData)) {
            m_completedTasks.insert(task);
        }
        allFinished = m_activeDownloads.isEmpty();
    }

    emit downloadFinished(success, errorString, userData);

    if (allFinished) {
        qInfo() << "所有下载任务已完成";
        emit allDownloadFinished();
    } else {
        qInfo() << "剩余活跃下载任务数:" << activeDownloads();
    }
}

void DownloadEngine::deleteTrackedTasks()
{
    QSet<DownloadTask*> tasks;
    {
        QMutexLocker locker(&m_mutex);
        for (auto* task : std::as_const(m_activeDownloads)) {
            tasks.insert(task);
        }
        tasks.unite(m_completedTasks);
        m_activeDownloads.clear();
        m_completedTasks.clear();
    }

    qDeleteAll(tasks);
}

void DownloadEngine::deleteCompletedTasks()
{
    QSet<DownloadTask*> tasks;
    {
        QMutexLocker locker(&m_mutex);
        tasks.swap(m_completedTasks);
    }

    qDeleteAll(tasks);
}

void DownloadEngine::waitForAllFinished()
{
    m_threadPool.waitForDone();
}

#ifdef CORE_REGRESSION_TESTS
void DownloadEngine::setTestReplyFactory(const std::function<QNetworkReply*(const QNetworkRequest&)>& replyFactory)
{
    m_testReplyFactory = replyFactory;
}

void DownloadEngine::clearTestReplyFactory()
{
    m_testReplyFactory = {};
}
#endif
