#include "../head/downloadprogresswindow.h"
#include "../head/downloadcoordinator.h"

#include <QCloseEvent>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

DownloadProgressWindow::DownloadProgressWindow(DownloadCoordinator* coordinator, QWidget* parent)
    : QWidget(parent, Qt::Window)
    , m_coordinator(coordinator)
{
    Q_ASSERT(m_coordinator != nullptr);

    setWindowTitle(QStringLiteral("下载进度"));
    setMinimumSize(560, 400);
    resize(700, 500);

    buildUi();
    connectCoordinator();
    refreshFromCoordinator();
}

DownloadProgressWindow::~DownloadProgressWindow()
{
    if (m_coordinator) {
        disconnect(m_coordinator, nullptr, this, nullptr);
    }
}

void DownloadProgressWindow::open()
{
    show();
    raise();
    activateWindow();
}

#ifdef CORE_REGRESSION_TESTS
void DownloadProgressWindow::setTestCloseConfirmationCallback(const std::function<QMessageBox::StandardButton()>& callback)
{
    m_testCloseConfirmationCallback = callback;
}

void DownloadProgressWindow::clearTestCloseConfirmationCallback()
{
    m_testCloseConfirmationCallback = nullptr;
}
#endif

void DownloadProgressWindow::buildUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(16, 16, 16, 16);
    rootLayout->setSpacing(10);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setTextFormat(Qt::PlainText);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(12);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    rootLayout->addWidget(m_titleLabel);

    m_queueLabel = new QLabel(this);
    m_queueLabel->setWordWrap(false);
    m_queueLabel->setTextFormat(Qt::PlainText);
    rootLayout->addWidget(m_queueLabel);

    m_stateLabel = new QLabel(this);
    m_stateLabel->setWordWrap(false);
    m_stateLabel->setTextFormat(Qt::PlainText);
    rootLayout->addWidget(m_stateLabel);

    m_currentProgressBar = new QProgressBar(this);
    m_currentProgressBar->setRange(0, 100);
    m_currentProgressBar->setValue(0);
    m_currentProgressBar->setTextVisible(true);
    rootLayout->addWidget(m_currentProgressBar);

    m_batchProgressBar = new QProgressBar(this);
    m_batchProgressBar->setRange(0, 100);
    m_batchProgressBar->setValue(0);
    m_batchProgressBar->setTextVisible(true);
    rootLayout->addWidget(m_batchProgressBar);

    m_detailModel = new DownloadModel(this);
    m_detailTable = new QTableView(this);
    m_detailTable->setModel(m_detailModel);
    m_detailTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_detailTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_detailTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_detailTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_detailTable->setTextElideMode(Qt::ElideMiddle);
    m_detailTable->setWordWrap(false);
    rootLayout->addWidget(m_detailTable, 1);

    m_messageLabel = new QLabel(this);
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setTextFormat(Qt::PlainText);
    rootLayout->addWidget(m_messageLabel);

    rootLayout->addStretch();

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);

    m_cancelCurrentButton = new QPushButton(QStringLiteral("取消当前"), this);
    m_cancelCurrentButton->setEnabled(false);
    connect(m_cancelCurrentButton, &QPushButton::clicked,
        this, &DownloadProgressWindow::requestCancelCurrent);
    buttonLayout->addWidget(m_cancelCurrentButton);

    m_cancelAllButton = new QPushButton(QStringLiteral("全部取消"), this);
    m_cancelAllButton->setEnabled(false);
    connect(m_cancelAllButton, &QPushButton::clicked,
        this, &DownloadProgressWindow::requestCancelAll);
    buttonLayout->addWidget(m_cancelAllButton);

    rootLayout->addLayout(buttonLayout);
}

void DownloadProgressWindow::connectCoordinator()
{
    connect(m_coordinator, &DownloadCoordinator::busyChanged,
        this, &DownloadProgressWindow::onCoordinatorBusyChanged);
    connect(m_coordinator, &DownloadCoordinator::batchStarted,
        this, &DownloadProgressWindow::onBatchStarted);
    connect(m_coordinator, &DownloadCoordinator::batchBusy,
        this, &DownloadProgressWindow::onBatchBusy);
    connect(m_coordinator, &DownloadCoordinator::jobChanged,
        this, &DownloadProgressWindow::onJobChanged);
    connect(m_coordinator, &DownloadCoordinator::jobFinished,
        this, &DownloadProgressWindow::onJobFinished);
    connect(m_coordinator, &DownloadCoordinator::batchProgress,
        this, &DownloadProgressWindow::onBatchProgress);
    connect(m_coordinator, &DownloadCoordinator::fatalBatchFailure,
        this, &DownloadProgressWindow::onFatalBatchFailure);
    connect(m_coordinator, &DownloadCoordinator::batchFinished,
        this, &DownloadProgressWindow::onBatchFinished);
    connect(m_coordinator, &DownloadCoordinator::shardInfoChanged,
        this, &DownloadProgressWindow::onShardInfoChanged);
}

void DownloadProgressWindow::requestCancelCurrent()
{
    if (m_coordinator) {
        m_coordinator->cancelCurrent();
    }
}

void DownloadProgressWindow::requestCancelAll()
{
    if (m_coordinator) {
        m_coordinator->cancelAll();
    }
}

void DownloadProgressWindow::refreshFromCoordinator()
{
    m_batchActive = m_coordinator->isBusy();

    if (m_batchActive) {
        const QList<DownloadJob> jobs = m_coordinator->jobs();
        updateBatchSummary(m_coordinator->completedJobs(),
            m_coordinator->failedJobs(),
            m_coordinator->cancelledJobs(),
            jobs.size());

        for (const DownloadJob& job : jobs) {
            if (job.state != DownloadJobState::Completed
                && job.state != DownloadJobState::Failed
                && job.state != DownloadJobState::Cancelled
                && job.state != DownloadJobState::Created
                && job.state != DownloadJobState::Queued) {
                updateCurrentJobDisplay(job);
                break;
            }
        }
    }

    m_cancelCurrentButton->setEnabled(m_batchActive);
    m_cancelAllButton->setEnabled(m_batchActive);
}

void DownloadProgressWindow::closeEvent(QCloseEvent* event)
{
    if (m_batchActive && m_coordinator) {
        QMessageBox::StandardButton reply = QMessageBox::No;
#ifdef CORE_REGRESSION_TESTS
        if (m_testCloseConfirmationCallback) {
            reply = m_testCloseConfirmationCallback();
        } else {
#endif
            reply = QMessageBox::question(
                this,
                QStringLiteral("取消下载"),
                QStringLiteral("下载任务正在进行，关闭窗口并取消全部任务吗？"),
                QMessageBox::Yes | QMessageBox::No);
#ifdef CORE_REGRESSION_TESTS
        }
#endif

        if (reply == QMessageBox::Yes) {
            requestCancelAll();
            hide();
            event->accept();
        } else {
            event->ignore();
        }
    } else {
        hide();
        event->accept();
    }
}

void DownloadProgressWindow::onCoordinatorBusyChanged(bool busy)
{
    m_batchActive = busy;
    m_cancelCurrentButton->setEnabled(busy);
    m_cancelAllButton->setEnabled(busy);

    if (!busy) {
        m_titleLabel->clear();
        m_currentProgressBar->setValue(0);
    }
}

void DownloadProgressWindow::onBatchStarted(int totalJobs)
{
    m_batchActive = true;
    m_cancelCurrentButton->setEnabled(true);
    m_cancelAllButton->setEnabled(true);
    m_messageLabel->clear();
    resetShardDetails();
    updateBatchSummary(0, 0, 0, totalJobs);
    m_batchProgressBar->setValue(0);
}

void DownloadProgressWindow::onBatchBusy()
{
    setMessage(QStringLiteral("下载协调器正忙"), true);
}

void DownloadProgressWindow::onJobChanged(const DownloadJob& job)
{
    if (job.state == DownloadJobState::Created || job.state == DownloadJobState::Queued) {
        return;
    }

    if (job.state == DownloadJobState::ResolvingM3u8) {
        resetShardDetails();
    }

    updateCurrentJobDisplay(job);
}

void DownloadProgressWindow::onJobFinished(const DownloadJob& job)
{
    setMessage(job.errorMessage, job.state == DownloadJobState::Failed || job.state == DownloadJobState::Cancelled);
}

void DownloadProgressWindow::onBatchProgress(int completedJobs, int failedJobs, int cancelledJobs, int totalJobs)
{
    updateBatchSummary(completedJobs, failedJobs, cancelledJobs, totalJobs);
}

void DownloadProgressWindow::onFatalBatchFailure(const DownloadJob& job, DownloadErrorCategory category, const QString& message)
{
    Q_UNUSED(category);
    setMessage(QStringLiteral("严重错误：%1 - %2").arg(job.request.videoTitle, message), true);
}

void DownloadProgressWindow::onBatchFinished(int completedJobs, int failedJobs, int cancelledJobs, int totalJobs, bool stoppedByFatalError)
{
    Q_UNUSED(stoppedByFatalError);

    m_batchActive = false;
    m_cancelCurrentButton->setEnabled(false);
    m_cancelAllButton->setEnabled(false);
    updateBatchSummary(completedJobs, failedJobs, cancelledJobs, totalJobs);
    m_batchProgressBar->setValue(100);
    m_titleLabel->clear();
    m_stateLabel->setText(QStringLiteral("批次完成"));
    m_currentProgressBar->setValue(0);
}

void DownloadProgressWindow::onShardInfoChanged(const DownloadInfo& info)
{
    if (m_detailModel) {
        m_detailModel->updateInfo(info);
    }
}

void DownloadProgressWindow::updateCurrentJobDisplay(const DownloadJob& job)
{
    m_titleLabel->setText(job.request.videoTitle);
    m_stateLabel->setText(QStringLiteral("状态：%1 / 阶段：%2")
        .arg(stateText(job.state), stageText(job.stage)));
    m_currentProgressBar->setValue(job.progressPercent);
}

void DownloadProgressWindow::updateBatchSummary(int completedJobs, int failedJobs, int cancelledJobs, int totalJobs)
{
    const int finished = completedJobs + failedJobs + cancelledJobs;
    m_queueLabel->setText(QStringLiteral("任务 %1/%2（完成：%3，失败：%4，取消：%5）")
        .arg(finished)
        .arg(totalJobs)
        .arg(completedJobs)
        .arg(failedJobs)
        .arg(cancelledJobs));

    if (totalJobs > 0) {
        m_batchProgressBar->setValue(finished * 100 / totalJobs);
    }
}

void DownloadProgressWindow::resetShardDetails()
{
    if (m_detailModel && m_detailModel->rowCount() > 0) {
        m_detailModel->removeRows(0, m_detailModel->rowCount());
    }
}

void DownloadProgressWindow::setMessage(const QString& message, bool isError)
{
    m_messageLabel->setText(messageText(message));
    if (message.isEmpty()) {
        return;
    }

    QPalette palette = m_messageLabel->palette();
    palette.setColor(QPalette::WindowText, isError ? Qt::red : Qt::black);
    m_messageLabel->setPalette(palette);
}

QString DownloadProgressWindow::stateText(DownloadJobState state) const
{
    switch (state) {
    case DownloadJobState::Created:         return QStringLiteral("已创建");
    case DownloadJobState::Queued:          return QStringLiteral("排队中");
    case DownloadJobState::ResolvingM3u8:   return QStringLiteral("解析中");
    case DownloadJobState::Downloading:     return QStringLiteral("下载中");
    case DownloadJobState::Concatenating:   return QStringLiteral("合并中");
    case DownloadJobState::Decrypting:      return QStringLiteral("解密中");
    case DownloadJobState::DirectFinalizing: return QStringLiteral("收尾中");
    case DownloadJobState::Completed:       return QStringLiteral("已完成");
    case DownloadJobState::Failed:          return QStringLiteral("失败");
    case DownloadJobState::Cancelled:       return QStringLiteral("已取消");
    }
    return QStringLiteral("未知");
}

QString DownloadProgressWindow::stageText(DownloadJobStage stage) const
{
    switch (stage) {
    case DownloadJobStage::None:               return QStringLiteral("空闲");
    case DownloadJobStage::FetchingPlaylist:   return QStringLiteral("获取播放列表");
    case DownloadJobStage::ParsingManifest:    return QStringLiteral("解析清单");
    case DownloadJobStage::DownloadingShards:  return QStringLiteral("下载分片");
    case DownloadJobStage::MergingShards:      return QStringLiteral("合并分片");
    case DownloadJobStage::RunningDecrypt:     return QStringLiteral("执行解密");
    case DownloadJobStage::ValidatingOutput:   return QStringLiteral("校验输出");
    case DownloadJobStage::PublishingOutput:   return QStringLiteral("发布文件");
    case DownloadJobStage::CleaningUp:         return QStringLiteral("清理中");
    }
    return QStringLiteral("未知");
}

QString DownloadProgressWindow::messageText(const QString& message) const
{
    if (message == QStringLiteral("cancelled")) {
        return QStringLiteral("已取消");
    }
    if (message == QStringLiteral("no segment urls")) {
        return QStringLiteral("没有可下载的分片地址");
    }
    if (message == QStringLiteral("batch stopped")) {
        return QStringLiteral("批次已停止");
    }

    return message;
}
