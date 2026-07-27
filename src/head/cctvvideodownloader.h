#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_ctvd.h"
#include "config.h"
#include "apiservice.h"

#include <tuple>
#include <optional>
#include <QSettings>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QMessageBox>
#include <QCheckBox>
#include <QPoint>
#include <QPointer>
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

    void onImportLinkSubmitted();

    void handleInlineImportColumnInfoResolved(quint64 requestId, const ContentParse::ImportResult& data);

    void handleInlineImportColumnInfoFailed(quint64 requestId, const QString& errorMessage);

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onCoordinatorBatchFinished(int completedJobs, int failedJobs, int cancelledJobs, int totalJobs, bool stoppedByFatalError);
    void onProgrammeContextMenuRequested(const QPoint& pos);
    void deleteSelectedProgrammes();

private:
    void updateImportAvailability();
    bool isInlineImportPending() const;
    void handleBrowseVideoListResolved(quint64 requestId, const QMap<int, VideoItem>& videos);
    void handlePreviewImageResolved(quint64 requestId, const QString& url, const QImage& image);
    void handleVideoInfoResolved(quint64 requestId, const QString& guid, const QString& channel, qint64 length);
    void handleVideoInfoFailed(quint64 requestId, const QString& guid, const QString& errorMessage);
    void renderVideoList(bool showHighlights);
    void updatePreviewImage();
    void showVideoDetails(const VideoItem& video);
    static QString formatDuration(qint64 seconds);

    Ui::MainWindow ui;
    QPixmap m_previewPixmap;
    quint64 m_pendingVideoListRequestId = 0;
    quint64 m_pendingImageRequestId = 0;
    quint64 m_pendingVideoInfoRequestId = 0;
    quint64 m_pendingInlineImportRequestId = 0;
    bool m_importDialogActive = false;
    bool m_pendingVideoListShowHighlights = false;
    QString m_pendingPreviewImageUrl;
    QString m_pendingVideoInfoGuid;
    inline static std::optional<ContentParse::ProgrammeRecord> SELECTED_PROGRAMME;
    inline static QMap<int, VideoItem> VIDEOS;

    DownloadCoordinator* m_downloadCoordinator = nullptr;
    QPointer<DownloadProgressWindow> m_downloadProgressWindow;
};
