#include "../head/downloadprogresswindow.h"
#include "../head/downloadcoordinator.h"
#include "../head/theme.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>

namespace {

class StatusDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        QStyleOptionViewItem base(option);
        initStyleOption(&base, index);
        const QString text = base.text;
        base.text.clear();
        QStyle* style = option.widget ? option.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &base, painter, option.widget);

        const auto status = static_cast<DownloadStatus>(index.data(Qt::UserRole).toInt());
        Theme::Role dotRole = Theme::Role::TextDisabled;
        switch (status) {
        case DownloadStatus::Downloading: dotRole = Theme::Role::Info; break;
        case DownloadStatus::Finished: dotRole = Theme::Role::Success; break;
        case DownloadStatus::Error: dotRole = Theme::Role::Danger; break;
        case DownloadStatus::Waiting: dotRole = Theme::Role::Warning; break;
        }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRect content = option.rect.adjusted(10, 0, -8, 0);
        const QPoint dotCenter(content.left() + 4, content.center().y());
        painter->setPen(Qt::NoPen);
        painter->setBrush(Theme::color(dotRole));
        painter->drawEllipse(dotCenter, 4, 4);

        const bool selected = option.state.testFlag(QStyle::State_Selected);
        painter->setPen(selected
                ? option.palette.color(QPalette::HighlightedText)
                : option.palette.color(QPalette::Text));
        painter->drawText(content.adjusted(16, 0, 0, 0),
            Qt::AlignVCenter | Qt::AlignLeft, text);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(size.height(), 32));
        size.setWidth(size.width() + 24);
        return size;
    }
};

class ProgressDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        QStyleOptionViewItem base(option);
        initStyleOption(&base, index);
        base.text.clear();
        QStyle* style = option.widget ? option.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &base, painter, option.widget);

        const int progress = qBound(0, index.data(Qt::UserRole).toInt(), 100);
        const QRect content = option.rect.adjusted(8, 8, -8, -8);
        const int labelWidth = option.fontMetrics.horizontalAdvance(QStringLiteral("100%")) + 6;
        const QRect barRect(content.left(), content.center().y() - 3,
            qMax(12, content.width() - labelWidth), 6);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(Theme::color(Theme::Role::Border));
        painter->drawRoundedRect(barRect, 3, 3);
        if (progress > 0) {
            QRect fill = barRect;
            fill.setWidth(qMax(6, barRect.width() * progress / 100));
            painter->setBrush(Theme::color(Theme::Role::Accent));
            painter->drawRoundedRect(fill, 3, 3);
        }

        const bool selected = option.state.testFlag(QStyle::State_Selected);
        painter->setPen(selected
                ? option.palette.color(QPalette::HighlightedText)
                : option.palette.color(QPalette::Text));
        painter->drawText(QRect(barRect.right() + 6, content.top(), labelWidth, content.height()),
            Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("%1%").arg(progress));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(size.height(), 32));
        size.setWidth(qMax(size.width(), 132));
        return size;
    }
};

} // namespace

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
    // m_coordinator is a QPointer: it may already be destroyed during app teardown.
    if (m_coordinator) {
        disconnect(m_coordinator.data(), nullptr, this, nullptr);
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
    rootLayout->setContentsMargins(20, 20, 20, 20);
    rootLayout->setSpacing(12);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName(QStringLiteral("downloadTitleLabel"));
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setTextFormat(Qt::PlainText);
    m_titleLabel->setProperty("textRole", "title");
    rootLayout->addWidget(m_titleLabel);

    m_queueLabel = new QLabel(this);
    m_queueLabel->setWordWrap(false);
    m_queueLabel->setTextFormat(Qt::PlainText);
    rootLayout->addWidget(m_queueLabel);

    m_stateLabel = new QLabel(this);
    m_stateLabel->setObjectName(QStringLiteral("downloadStateLabel"));
    m_stateLabel->setWordWrap(false);
    m_stateLabel->setTextFormat(Qt::PlainText);
    m_stateLabel->setProperty("textRole", "caption");
    rootLayout->addWidget(m_stateLabel);

    auto* currentProgressLabel = new QLabel(QStringLiteral("当前任务"), this);
    currentProgressLabel->setObjectName(QStringLiteral("currentProgressLabel"));
    currentProgressLabel->setProperty("textRole", "section");
    rootLayout->addWidget(currentProgressLabel);

    m_currentProgressBar = new QProgressBar(this);
    m_currentProgressBar->setRange(0, 100);
    m_currentProgressBar->setValue(0);
    m_currentProgressBar->setTextVisible(false);
    rootLayout->addWidget(m_currentProgressBar);

    auto* batchProgressLabel = new QLabel(QStringLiteral("批次总计"), this);
    batchProgressLabel->setObjectName(QStringLiteral("batchProgressLabel"));
    batchProgressLabel->setProperty("textRole", "section");
    rootLayout->addWidget(batchProgressLabel);

    m_batchProgressBar = new QProgressBar(this);
    m_batchProgressBar->setRange(0, 100);
    m_batchProgressBar->setValue(0);
    m_batchProgressBar->setTextVisible(false);
    rootLayout->addWidget(m_batchProgressBar);

    m_detailModel = new DownloadModel(this);
    m_detailTable = new QTableView(this);
    m_detailTable->setModel(m_detailModel);
    m_detailTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_detailTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_detailTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_detailTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_detailTable->horizontalHeader()->setHighlightSections(false);
    m_detailTable->verticalHeader()->setVisible(false);
    m_detailTable->setTextElideMode(Qt::ElideMiddle);
    m_detailTable->setWordWrap(false);
    m_detailTable->setShowGrid(false);
    m_detailTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_detailTable->setAlternatingRowColors(true);
    m_detailTable->setItemDelegateForColumn(1, new StatusDelegate(m_detailTable));
    m_detailTable->setItemDelegateForColumn(3, new ProgressDelegate(m_detailTable));
    m_detailTable->verticalHeader()->setDefaultSectionSize(32);
    rootLayout->addWidget(m_detailTable, 1);

    m_messageLabel = new QLabel(this);
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setTextFormat(Qt::PlainText);
    m_messageLabel->setVisible(false);
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

    m_closeButton = new QPushButton(QStringLiteral("关闭"), this);
    m_closeButton->setObjectName(QStringLiteral("downloadCloseButton"));
    m_closeButton->setProperty("buttonRole", "primary");
    m_closeButton->setVisible(false);
    connect(m_closeButton, &QPushButton::clicked, this, [this]() { close(); });
    buttonLayout->addWidget(m_closeButton);

    rootLayout->addLayout(buttonLayout);
}

void DownloadProgressWindow::connectCoordinator()
{
    if (!m_coordinator) {
        return;
    }
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
    m_cancelCurrentButton->setVisible(m_batchActive);
    m_cancelAllButton->setVisible(m_batchActive);
    m_closeButton->setVisible(!m_batchActive);
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
    m_cancelCurrentButton->setVisible(busy);
    m_cancelAllButton->setVisible(busy);
    m_closeButton->setVisible(!busy);

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
    m_cancelCurrentButton->setVisible(true);
    m_cancelAllButton->setVisible(true);
    m_closeButton->setVisible(false);
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
    m_cancelCurrentButton->setVisible(false);
    m_cancelAllButton->setVisible(false);
    m_closeButton->setVisible(true);
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
    const QString translatedMessage = messageText(message);
    m_messageLabel->setText(translatedMessage);
    m_messageLabel->setVisible(!translatedMessage.isEmpty());
    if (translatedMessage.isEmpty()) {
        return;
    }

    m_messageLabel->setProperty("severity", isError ? "danger" : "info");
    m_messageLabel->style()->unpolish(m_messageLabel);
    m_messageLabel->style()->polish(m_messageLabel);
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
    if (message.startsWith(QStringLiteral("batch stopped: "))) {
        return QStringLiteral("批次已停止：%1").arg(messageText(message.mid(15)));
    }
    if (message == QStringLiteral("concat stage busy")) {
        return QStringLiteral("合并阶段正忙");
    }
    if (message == QStringLiteral("decrypt stage busy")) {
        return QStringLiteral("解密阶段正忙");
    }
    if (message == QStringLiteral("direct finalize stage busy") || message == QStringLiteral("direct_finalize_busy")) {
        return QStringLiteral("收尾阶段正忙");
    }

    return message;
}
