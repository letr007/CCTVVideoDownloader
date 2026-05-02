#include "../head/cctvvideodownloader.h"
#include "../head/about.h"
#include "../head/setting.h"
#include "../head/import.h"
#include "../head/downloaddialog.h"
#include "../head/concat.h"
#include "../head/decrypt.h"
#include "../head/apiservice.h"
#include "../head/directmediafinalizer.h"
#include "../head/logger.h"
#include <algorithm>
#include <QResizeEvent>

#include <QSizePolicy>

//std::tuple<int, int> CCTVVideoDownloader::SELECTED_ID;

CCTVVideoDownloader::CCTVVideoDownloader(QWidget* parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);
    setMinimumSize(640, 480);
    setMaximumSize(960, 720);
    resize(800, 600);
    ui.leftPane->setMinimumWidth(180);
    ui.rightPane->setMinimumWidth(180);
    ui.mainSplitter->setStretchFactor(0, 1);
    ui.mainSplitter->setStretchFactor(1, 1);
    ui.mainSplitter->setSizes({ 240, 240 });
    ui.label_img->setScaledContents(false);
    ui.label_img->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    ui.label_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    ui.label_title->setWordWrap(true);
    ui.label_introduce->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    ui.label_time->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    // 设置标题和图标
    setWindowTitle(QString("央视视频下载器"));
    setWindowIcon(QIcon(QPixmap(":/cctvvideodownload.png")));
    // 初始化全局设置
    initGlobalSettings();
    // 初始化日志系统
    Logger::instance()->init("cctvvideodownloader.log");
    // 从配置读取日志级别并设置
    int logLevel = readLogLevel();
    Logger::instance()->setLogLevel(logLevel);
    // 连接槽函数
    signalConnect();

    flashProgrammeList();
}

CCTVVideoDownloader::~CCTVVideoDownloader()
{}

void CCTVVideoDownloader::signalConnect()
{
    connect(ui.actionexit, &QAction::triggered, this, &CCTVVideoDownloader::close); // 退出
    connect(ui.actionabout, &QAction::triggered, this, &CCTVVideoDownloader::openAboutDialog); // 关于
    connect(ui.actionsetting, &QAction::triggered, this, &CCTVVideoDownloader::openSettingDialog); // 设置
    connect(ui.settings, &QPushButton::clicked, this, &CCTVVideoDownloader::openSettingDialog); // 设置
    connect(ui.actionimport, &QAction::triggered, this, &CCTVVideoDownloader::openImportDialog); // 导入
    connect(ui.actionfile, &QAction::triggered, this, &CCTVVideoDownloader::openSaveDir); // 打开视频保存位置
    connect(ui.flash_program, &QPushButton::clicked, this, &CCTVVideoDownloader::flashProgrammeList); // 刷新节目
    connect(ui.mainSplitter, &QSplitter::splitterMoved, this, &CCTVVideoDownloader::updatePreviewImage);
    connect(ui.tableWidget_Config, &QTableWidget::cellClicked, this, &CCTVVideoDownloader::isProgrammeSelected); // 栏目表格点击
    connect(ui.flash_list, &QPushButton::clicked, this, &CCTVVideoDownloader::flashVideoList); // 刷新视频
    connect(ui.tableWidget_List, &QTableWidget::cellClicked, this, &CCTVVideoDownloader::isVideoSelected); // 刷新信息
    connect(ui.pushButton, &QPushButton::clicked, this, &CCTVVideoDownloader::openDownloadDialog); // 下载
    connect(ui.btn_select_all, &QPushButton::clicked, this, &CCTVVideoDownloader::toggleSelectAllVideos); // 全选视频
}

void CCTVVideoDownloader::flashProgrammeList()
{
    qInfo() << "刷新节目列表";
    
    // 读取配置文件
    QList<QJsonObject> programmes = readProgrammeFromConfig();
    qInfo() << "从配置读取到" << programmes.size() << "个节目";

    // 获取表格控件
    QTableWidget* table = ui.tableWidget_Config;

    // 设置表格行数
    table->setRowCount(programmes.size());

    // 填充表格数据
    for (int row = 0; row < programmes.size(); ++row) {
        const QJsonObject& programme = programmes[row];

        // 创建表格项
        QTableWidgetItem* nameItem = new QTableWidgetItem(programme["name"].toString());
        QTableWidgetItem* columnIdItem = new QTableWidgetItem(programme["columnid"].toString());
        QTableWidgetItem* itemIdItem = new QTableWidgetItem(programme["itemid"].toString());

        // 设置表格项为不可编辑
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        columnIdItem->setFlags(columnIdItem->flags() & ~Qt::ItemIsEditable);
        itemIdItem->setFlags(itemIdItem->flags() & ~Qt::ItemIsEditable);

        // 加入表格
        table->setItem(row, 0, nameItem);
        table->setItem(row, 1, columnIdItem);
        table->setItem(row, 2, itemIdItem);
    }

    // 自动调整列宽
    table->resizeColumnsToContents();
    
    qInfo() << "节目列表刷新完成，显示" << programmes.size() << "个节目";
}

void CCTVVideoDownloader::flashVideoList()
{
    qInfo() << "刷新视频列表";
    
    if (!SELECTED_ID) {
        qWarning() << "刷新视频列表失败: SELECTED_ID为空";
        return;
    }
    auto [columnId, itemId] = *SELECTED_ID;
    auto [displayMin, displayMax] = readDisplayMinAndMax();
    
    qInfo() << "获取视频列表参数 - columnId:" << columnId << "itemId:" << itemId
             << "显示范围:" << displayMin << "-" << displayMax;
    
    VIDEOS = APIService::instance().getVideoList(
        columnId,
        itemId,
        displayMin,
        displayMax
    );

    const bool showHighlights = readShowHighlights();
    int highlightCount = 0;
    int fragmentCount = 0;
    auto appendExtraVideos = [this](const QMap<int, VideoItem>& extras, int& appendedCount) {
        int nextIndex = VIDEOS.isEmpty() ? 0 : (VIDEOS.lastKey() + 1);
        for (const VideoItem& item : std::as_const(extras)) {
            bool alreadyListed = false;
            for (const VideoItem& existing : std::as_const(VIDEOS)) {
                if (!item.guid.isEmpty() && item.guid == existing.guid) {
                    alreadyListed = true;
                    break;
                }
            }
            if (alreadyListed) {
                continue;
            }
            VIDEOS.insert(nextIndex++, item);
            ++appendedCount;
        }
    };

    if (showHighlights) {
        QMap<int, VideoItem> highlights = APIService::instance().getHighlightList(itemId);
        appendExtraVideos(highlights, highlightCount);

        QMap<int, VideoItem> fragments = APIService::instance().getFragmentList(columnId, itemId);
        appendExtraVideos(fragments, fragmentCount);
        qInfo() << "Extra video counts - highlights:" << highlightCount << "fragments:" << fragmentCount;
    }
    
    qInfo() << "获取到" << VIDEOS.size() << "个视频，其中看点" << highlightCount << "个";
    
    // 显示结果
    ui.tableWidget_List->clearContents();
    ui.tableWidget_List->setRowCount(VIDEOS.size());
    ui.tableWidget_List->setColumnCount(showHighlights ? 3 : 2);

    ui.tableWidget_List->setColumnWidth(0, 30);
    ui.tableWidget_List->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    ui.tableWidget_List->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    if (showHighlights) {
        ui.tableWidget_List->setColumnWidth(2, 56);
        ui.tableWidget_List->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    }

    // 隐藏行头
    ui.tableWidget_List->verticalHeader()->setVisible(false);

    // 设置列标题
    ui.tableWidget_List->setHorizontalHeaderLabels(showHighlights
        ? QStringList{ "", "视频标题", "看点" }
        : QStringList{ "", "视频标题" });

    int row = 0;
    for (auto&& [index, item] : std::as_const(VIDEOS).asKeyValueRange()) {
        // 在第一列创建复选框
        QTableWidgetItem* checkItem = new QTableWidgetItem();
        checkItem->setCheckState(Qt::Unchecked);
        checkItem->setFlags(checkItem->flags() | Qt::ItemIsUserCheckable ^ Qt::ItemIsEditable);
        checkItem->setTextAlignment(Qt::AlignCenter); // 居中对齐
        ui.tableWidget_List->setItem(row, 0, checkItem);

        // 在第二列创建标题项
        QTableWidgetItem* titleItem = new QTableWidgetItem(item.title);
        titleItem->setFlags(titleItem->flags() ^ Qt::ItemIsEditable); // 设为不可编辑
        ui.tableWidget_List->setItem(row, 1, titleItem);

        if (showHighlights) {
            QTableWidgetItem* highlightItem = new QTableWidgetItem(item.isHighlight ? QStringLiteral("看点") : QStringLiteral("完整"));
            highlightItem->setText(item.isHighlight ? item.listType : QStringLiteral("完整"));
            highlightItem->setFlags(highlightItem->flags() ^ Qt::ItemIsEditable);
            highlightItem->setTextAlignment(Qt::AlignCenter);
            ui.tableWidget_List->setItem(row, 2, highlightItem);
        }

        row++;
    }
    ui.tableWidget_List->viewport()->update();
    ui.btn_select_all->setText("全选");
    
    qInfo() << "视频列表刷新完成，显示" << VIDEOS.size() << "个视频";
}

void CCTVVideoDownloader::isProgrammeSelected(int r, int c)
{
    // 获取ID
    auto selectedItemColumnId = ui.tableWidget_Config->item(r, 1)->text();
    auto selectedItemItemId = ui.tableWidget_Config->item(r, 2)->text();
    // 获取名称
    auto selected_item_name = ui.tableWidget_Config->item(r, 0)->text();
    
    qInfo() << "选中栏目 - 行:" << r << "列:" << c << "名称:" << selected_item_name
             << "columnId:" << selectedItemColumnId << "itemId:" << selectedItemItemId;
    
    SELECTED_ID.emplace(selectedItemColumnId, selectedItemItemId);
    flashVideoList();
}

void CCTVVideoDownloader::isVideoSelected(int r, int c)
{
	QApplication::keyboardModifiers();
	if (c == 1 && QApplication::keyboardModifiers() & Qt::ControlModifier) {
        // Ctrl + 点击标题列，切换复选框状态
        auto item =  ui.tableWidget_List->item(r, 0);
        if (item->checkState() == Qt::Checked) {
            item->setCheckState(Qt::Unchecked);
        } else {
            item->setCheckState(Qt::Checked);
        }
    }

    auto selectedIndex = ui.tableWidget_List->currentRow();
	//qDebug() << "选中视频索引:" << selectedIndex;
    // 获取当前视频信息
    auto it = VIDEOS.find(selectedIndex);
    if (it == VIDEOS.end())
    {
        qWarning() << "无效的video index:" << selectedIndex;
        return;
    }
    auto title = it->title;
    auto GUID = it->guid;
    auto brief = it->brief;
    auto time = it->time;
    auto imageUrl = it->image;

    //qDebug() << title << GUID;
    DOWNLOAD_META_INFO.emplace(title, GUID, false);
    // 文本处理
    brief.replace(' ', '\n').replace('\r', '\n');
    brief.replace(QRegularExpression("\n+"), "\n");
    time.replace(' ', '\n');
    //qDebug() << brief;
    // 从API获取图片
    auto image = APIService::instance().getImage(imageUrl);
    if (!image.isNull())
    {
        m_previewPixmap = QPixmap::fromImage(image);
        updatePreviewImage();
    }
    else
    {
        m_previewPixmap = QPixmap();
        ui.label_img->setText("图片加载失败");
    }
    ui.label_title->setText(title);
    ui.label_introduce->setText(brief);
    ui.label_introduce->setWordWrap(true);
    ui.label_time->setText(time);
    
}

void CCTVVideoDownloader::toggleSelectAllVideos()
{
    int rows = ui.tableWidget_List->rowCount();
    if (rows == 0) {
        return;
    }

    bool allChecked = true;
    for (int r = 0; r < rows; ++r) {
        auto item = ui.tableWidget_List->item(r, 0);
        if (!item || item->checkState() != Qt::Checked) {
            allChecked = false;
            break;
        }
    }

    Qt::CheckState newState = allChecked ? Qt::Unchecked : Qt::Checked;
    for (int r = 0; r < rows; ++r) {
        auto item = ui.tableWidget_List->item(r, 0);
        if (item) {
            item->setCheckState(newState);
        }
    }

    ui.btn_select_all->setText(allChecked ? "全选" : "取消");
}

void CCTVVideoDownloader::openAboutDialog()
{
    qInfo() << "打开关于对话框";
    About aboutDialog(this);
    aboutDialog.exec();
    qInfo() << "关于对话框已关闭";
}

void CCTVVideoDownloader::openSettingDialog()
{
    qInfo() << "打开设置对话框";
    Setting setttingDialog(this);
    setttingDialog.exec();
    qInfo() << "设置对话框已关闭";
}

void CCTVVideoDownloader::openSaveDir()
{
    qInfo() << "打开保存目录";
    g_settings->beginGroup("settings");
    auto saveDir = g_settings->value("save_dir").toString();
    auto url = QUrl::fromLocalFile(saveDir);
    g_settings->endGroup();
    
    qInfo() << "保存目录:" << saveDir;
    
    // 使用QDesktopServices打开路径
    if (!QDesktopServices::openUrl(url)) {
        qWarning() << "打开文件保存位置失败:" << saveDir;
        QMessageBox::warning(nullptr, "Error", "打开文件保存位置失败");
    } else {
        qInfo() << "成功打开保存目录";
    }
}

void CCTVVideoDownloader::openImportDialog()
{
    qInfo() << "打开导入对话框";
    Import importDialog(this);
    connect(&importDialog, &Import::importFinished, this, &CCTVVideoDownloader::flashProgrammeList);
    importDialog.exec();
    qInfo() << "导入对话框已关闭";
}

void CCTVVideoDownloader::openDownloadDialog()
{
    qInfo() << "打开下载对话框";
    
    // 检查列表中被选中的项
    QString savePath = readSavePath();
    int threadNum = readThreadNum();
    QList<int> selectedIndexes;

    int rows = ui.tableWidget_List->rowCount();
    for (int r = 0; r < rows; ++r) {
        auto item = ui.tableWidget_List->item(r, 0);
        if (item && item->checkState() == Qt::Checked) {
            selectedIndexes.push_back(r);
        }
    }

    qInfo() << "选中" << selectedIndexes.size() << "个视频进行下载，保存路径:" << savePath << "线程数:" << threadNum;

    // 如果没有选中任何项，使用单个选中视频
    if (selectedIndexes.empty()) {
        qInfo() << "未选中批量下载，使用单个视频下载";
        if (!DOWNLOAD_META_INFO.has_value()) {
            qWarning() << "下载失败: 未选择要下载的视频";
            QMessageBox::warning(this, "Warning", "请先选择要下载的视频！");
            return;
        }
        auto [title, GUID, is4K] = *DOWNLOAD_META_INFO;
        qInfo() << "单个视频下载 - 标题:" << title << "GUID:" << GUID;

        QStringList URLS = APIService::instance().getEncryptM3U8Urls(
            GUID,
            readQuality()
        );
        DOWNLOAD_META_INFO.emplace(title, GUID, APIService::instance().lastM3U8ResultWas4K());

        qInfo() << "获取到" << URLS.size() << "个TS文件URL";
        if (URLS.isEmpty()) {
            qWarning() << "下载失败: 未获取到任何TS切片URL，GUID:" << GUID;
            QMessageBox::warning(this, "Warning", "未获取到可下载的TS切片，请稍后重试或查看日志。");
            return;
        }

        Download dialog(this);
        // 先关闭下载窗口再进行完成后操作
        connect(&dialog, &Download::DownloadFinished, this, [&dialog](bool success) {
            qInfo() << "下载完成，关闭下载对话框";
            success ? dialog.accept() : dialog.reject();
            });
        dialog.transferDwonloadParams(title, URLS, savePath, threadNum);
        dialog.setModal(true);
        const int result = dialog.exec();
        qInfo() << "下载对话框已关闭";
        if (result == QDialog::Accepted) {
            concatVideo();
        }
        return;
    }

    // 按选中顺序逐个处理
    qInfo() << "开始批量下载" << selectedIndexes.size() << "个视频";
    for (int idx : selectedIndexes) {
        auto it = VIDEOS.find(idx);
        if (it == VIDEOS.end()) {
            qWarning() << "无效的video index(批量):" << idx;
            continue;
        }
        auto title = it->title;
        auto GUID = it->guid;

        qInfo() << "批量下载第" << idx << "个视频 - 标题:" << title << "GUID:" << GUID;

        // 设置当前下载元信息，便于后续 concat/decrypt 使用
        DOWNLOAD_META_INFO.emplace(title, GUID, false);

        QStringList URLS = APIService::instance().getEncryptM3U8Urls(
            GUID,
            readQuality()
        );
        DOWNLOAD_META_INFO.emplace(title, GUID, APIService::instance().lastM3U8ResultWas4K());

        qInfo() << "获取到" << URLS.size() << "个TS文件URL";
        if (URLS.isEmpty()) {
            qWarning() << "批量下载失败: 未获取到任何TS切片URL，GUID:" << GUID;
            QMessageBox::warning(this, "Warning", QString("视频 \"%1\" 未获取到可下载的TS切片，已跳过。").arg(title));
            continue;
        }

        Download dialog(this);
        connect(&dialog, &Download::DownloadFinished, this, [&dialog](bool success) {
            qInfo() << "下载完成，关闭下载对话框";
            success ? dialog.accept() : dialog.reject();
            });
        dialog.transferDwonloadParams(title, URLS, savePath, threadNum);
        dialog.setModal(true);
        const int result = dialog.exec();
        qInfo() << "下载对话框已关闭";
        if (result != QDialog::Accepted) {
            qInfo() << "批量下载已取消或失败，停止后续任务";
            break;
        }
        concatVideo();
    }
    
    qInfo() << "批量下载全部完成";
}

void CCTVVideoDownloader::concatVideo()
{
    if (!DOWNLOAD_META_INFO.has_value()) {
        qWarning() << "视频拼接失败: 下载元信息为空";
        return;
    }
    
    auto [title, GUID, is4K] = *DOWNLOAD_META_INFO;
    QString savePath = readSavePath();
    
    qInfo() << "开始视频拼接 - 标题:" << title << "GUID:" << GUID << "保存路径:" << savePath;
    
    Concat concatDialog(this);
    connect(&concatDialog, &Concat::ConcatFinished, this, &CCTVVideoDownloader::decryptVideo);
    concatDialog.transferConcatParams(title, savePath);
    concatDialog.exec();
    
    qInfo() << "视频拼接对话框已关闭";
}

void CCTVVideoDownloader::decryptVideo()
{
    if (!DOWNLOAD_META_INFO.has_value()) {
        qWarning() << "视频解密失败: 下载元信息为空";
        return;
	}
    
    auto [title, GUID, is4K] = *DOWNLOAD_META_INFO;
    QString savePath = readSavePath();
    if (is4K) {
        qInfo() << "当前视频为CCTV-4K，跳过解密步骤，发布已验证的 TS 阶段文件";
        const DirectMediaFinalizeResult finalizeResult = finalizeDirectTsTask(title,
            savePath,
            readTranscode());
        if (!finalizeResult.ok) {
            qCritical() << "CCTV-4K 最终发布失败:" << finalizeResult.code << finalizeResult.message;
            QMessageBox::warning(this,
                "Warning",
                QString::fromUtf8("CCTV-4K 视频最终发布失败 [%1]: %2")
                    .arg(finalizeResult.code, finalizeResult.message));
            return;
        }
        qInfo() << "CCTV-4K视频处理完成，输出文件:" << finalizeResult.finalPath;
        return;
    }
    
    qInfo() << "开始视频解密 - 标题:" << title << "GUID:" << GUID << "保存路径:" << savePath;
    
    Decrypt decryptDialog(this);
    decryptDialog.transferDecryptParams(title, savePath, readTranscode());
    decryptDialog.exec();
    
    qInfo() << "视频解密对话框已关闭";
}

void CCTVVideoDownloader::updatePreviewImage()
{
    if (m_previewPixmap.isNull())
        return;

    int desiredHeight = ui.rightPane->width() * 9 / 16;
    desiredHeight = qBound(120, desiredHeight, 220);

    ui.label_img->setFixedHeight(desiredHeight);

    QPixmap scaled = m_previewPixmap.scaled(ui.label_img->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    ui.label_img->setPixmap(scaled);
}

void CCTVVideoDownloader::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updatePreviewImage();
}
