#pragma once

#include <QWidget>

#ifdef CORE_REGRESSION_TESTS
#include <functional>
#include <QMessageBox>
#endif

#include "downloadjob.h"
#include "downloadmodel.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QTableView;
class QVBoxLayout;
class DownloadCoordinator;

class DownloadProgressWindow : public QWidget
{
    Q_OBJECT

public:
    explicit DownloadProgressWindow(DownloadCoordinator* coordinator, QWidget* parent = nullptr);
    ~DownloadProgressWindow() override;

    void open();

#ifdef CORE_REGRESSION_TESTS
    void setTestCloseConfirmationCallback(const std::function<QMessageBox::StandardButton()>& callback);
    void clearTestCloseConfirmationCallback();
#endif

public slots:
    void refreshFromCoordinator();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onCoordinatorBusyChanged(bool busy);
    void onBatchStarted(int totalJobs);
    void onBatchBusy();
    void onJobChanged(const DownloadJob& job);
    void onJobFinished(const DownloadJob& job);
    void onBatchProgress(int completedJobs, int failedJobs, int cancelledJobs, int totalJobs);
    void onFatalBatchFailure(const DownloadJob& job, DownloadErrorCategory category, const QString& message);
    void onBatchFinished(int completedJobs, int failedJobs, int cancelledJobs, int totalJobs, bool stoppedByFatalError);
    void onShardInfoChanged(const DownloadInfo& info);

private:
    void buildUi();
    void connectCoordinator();
    void requestCancelCurrent();
    void requestCancelAll();
    void updateCurrentJobDisplay(const DownloadJob& job);
    void updateBatchSummary(int completedJobs, int failedJobs, int cancelledJobs, int totalJobs);
    void resetShardDetails();
    void setMessage(const QString& message, bool isError);
    QString stateText(DownloadJobState state) const;
    QString stageText(DownloadJobStage stage) const;
    QString messageText(const QString& message) const;

    DownloadCoordinator* m_coordinator;
    bool m_batchActive = false;

#ifdef CORE_REGRESSION_TESTS
    std::function<QMessageBox::StandardButton()> m_testCloseConfirmationCallback;
#endif

    QLabel* m_titleLabel = nullptr;
    QLabel* m_queueLabel = nullptr;
    QLabel* m_stateLabel = nullptr;
    QProgressBar* m_currentProgressBar = nullptr;
    QProgressBar* m_batchProgressBar = nullptr;
    DownloadModel* m_detailModel = nullptr;
    QTableView* m_detailTable = nullptr;
    QLabel* m_messageLabel = nullptr;
    QPushButton* m_cancelCurrentButton = nullptr;
    QPushButton* m_cancelAllButton = nullptr;
    QPushButton* m_closeButton = nullptr;
};
