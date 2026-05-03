#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_ctvd.h"
#include "config.h"
#include "apiservice.h"

#include <Tuple>
#include <Optional>
#include <QSettings>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QMessageBox>
#include <QCheckBox>
#include <QPixmap>

class QResizeEvent;
class DownloadCoordinator;
class DownloadProgressWindow;

class CCTVVideoDownloader : public QMainWindow
{
    Q_OBJECT

public:
    CCTVVideoDownloader(QWidget *parent = nullptr);
    ~CCTVVideoDownloader();
    void signalConnect(); // 初始化连接信号与槽

    void flashProgrammeList();

    void flashVideoList();

    void isProgrammeSelected(int r, int c);

    void isVideoSelected(int r, int c);

    void openAboutDialog();

    void openSettingDialog();

    void openSaveDir();

    void openImportDialog();

    void openDownloadDialog();

    void ImportProgrammeFromUrl();

    void toggleSelectAllVideos();

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onCoordinatorBatchFinished(int completedJobs, int failedJobs, int cancelledJobs, int totalJobs, bool stoppedByFatalError);

private:
    void handleBrowseVideoListResolved(quint64 requestId, const QMap<int, VideoItem>& videos);
    void handlePreviewImageResolved(quint64 requestId, const QString& url, const QImage& image);
    void renderVideoList(bool showHighlights);
    void updatePreviewImage();

    Ui::MainWindow ui;
    QPixmap m_previewPixmap;
    quint64 m_pendingVideoListRequestId = 0;
    quint64 m_pendingImageRequestId = 0;
    bool m_pendingVideoListShowHighlights = false;
    QString m_pendingPreviewImageUrl;
    inline static std::optional<std::tuple<QString, QString>> SELECTED_ID;
    inline static QMap<int, VideoItem> VIDEOS;

    DownloadCoordinator* m_downloadCoordinator = nullptr;
    DownloadProgressWindow* m_downloadProgressWindow = nullptr;
};
