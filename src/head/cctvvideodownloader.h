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

    void concatVideo();

    void decryptVideo();

    void ImportProgrammeFromUrl();

private:
    Ui::MainWindow ui;
    inline static std::optional<std::tuple<QString, QString>> SELECTED_ID;
    inline static std::optional<std::tuple<QString, QString>> DOWNLOAD_META_INFO;
    inline static QMap<int, VideoItem> VIDEOS;
};
