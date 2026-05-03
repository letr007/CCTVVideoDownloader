#include "../head/cctvvideodownloader.h"
#include "../head/about.h"
#include "../head/setting.h"
#include "../head/import.h"
#include "../head/apiservice.h"
#include "../head/downloadcoordinator.h"
#include "../head/downloadjob.h"
#include "../head/downloadprogresswindow.h"
#include "../head/logger.h"
#include <algorithm>
#include <QResizeEvent>
#include <QStatusBar>

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
    m_downloadCoordinator = new DownloadCoordinator(&APIService::instance(), nullptr, nullptr, nullptr, this);
    m_downloadProgressWindow = new DownloadProgressWindow(m_downloadCoordinator, this);

    connect(m_downloadCoordinator, &DownloadCoordinator::batchFinished,
        this, &CCTVVideoDownloader::onCoordinatorBatchFinished);

    flashProgrammeList();
}

CCTVVideoDownloader::~CCTVVideoDownloader()
{
}

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
    connect(&APIService::instance(), &APIService::browseVideoListResolved, this, &CCTVVideoDownloader::handleBrowseVideoListResolved);
    connect(&APIService::instance(), &APIService::imageResolved, this, &CCTVVideoDownloader::handlePreviewImageResolved);
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
    
    m_pendingVideoListShowHighlights = readShowHighlights();
    m_pendingVideoListRequestId = APIService::instance().startGetBrowseVideoList(
        columnId,
        itemId,
        displayMin,
        displayMax,
        m_pendingVideoListShowHighlights
    );
    ui.flash_list->setEnabled(false);
    ui.tableWidget_List->setEnabled(false);
    ui.btn_select_all->setEnabled(false);
    qInfo() << "已发起异步视频列表刷新，请求ID:" << m_pendingVideoListRequestId;
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

    // 文本处理
    brief.replace(' ', '\n').replace('\r', '\n');
    brief.replace(QRegularExpression("\n+"), "\n");
    time.replace(' ', '\n');
    //qDebug() << brief;
    ui.label_title->setText(title);
    ui.label_introduce->setText(brief);
    ui.label_introduce->setWordWrap(true);
    ui.label_time->setText(time);

    m_pendingPreviewImageUrl = imageUrl;
    m_previewPixmap = QPixmap();
    ui.label_img->setPixmap(QPixmap());
    ui.label_img->setText(imageUrl.isEmpty() ? QStringLiteral("图片加载失败") : QStringLiteral("图片加载中..."));
    if (!imageUrl.isEmpty()) {
        m_pendingImageRequestId = APIService::instance().startGetImage(imageUrl);
    }
}

void CCTVVideoDownloader::handleBrowseVideoListResolved(quint64 requestId, const QMap<int, VideoItem>& videos)
{
    if (requestId != m_pendingVideoListRequestId) {
        return;
    }

    VIDEOS = videos;
    qInfo() << "异步视频列表刷新完成，请求ID:" << requestId << "视频数量:" << VIDEOS.size();
    renderVideoList(m_pendingVideoListShowHighlights);
    ui.flash_list->setEnabled(true);
    ui.tableWidget_List->setEnabled(true);
    ui.btn_select_all->setEnabled(true);
}

void CCTVVideoDownloader::handlePreviewImageResolved(quint64 requestId, const QString& url, const QImage& image)
{
    if (requestId != m_pendingImageRequestId || url != m_pendingPreviewImageUrl) {
        return;
    }

    if (image.isNull()) {
        m_previewPixmap = QPixmap();
        ui.label_img->setPixmap(QPixmap());
        ui.label_img->setText(QStringLiteral("图片加载失败"));
        return;
    }

    m_previewPixmap = QPixmap::fromImage(image);
    ui.label_img->setText(QString());
    updatePreviewImage();
}

void CCTVVideoDownloader::renderVideoList(bool showHighlights)
{
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

    ui.tableWidget_List->verticalHeader()->setVisible(false);
    ui.tableWidget_List->setHorizontalHeaderLabels(showHighlights
        ? QStringList{ "", "视频标题", "看点" }
        : QStringList{ "", "视频标题" });

    int row = 0;
    for (auto&& [index, item] : std::as_const(VIDEOS).asKeyValueRange()) {
        Q_UNUSED(index);
        QTableWidgetItem* checkItem = new QTableWidgetItem();
        checkItem->setCheckState(Qt::Unchecked);
        checkItem->setFlags(checkItem->flags() | Qt::ItemIsUserCheckable ^ Qt::ItemIsEditable);
        checkItem->setTextAlignment(Qt::AlignCenter);
        ui.tableWidget_List->setItem(row, 0, checkItem);

        QTableWidgetItem* titleItem = new QTableWidgetItem(item.title);
        titleItem->setFlags(titleItem->flags() ^ Qt::ItemIsEditable);
        ui.tableWidget_List->setItem(row, 1, titleItem);

        if (showHighlights) {
            QTableWidgetItem* highlightItem = new QTableWidgetItem(item.isHighlight ? QStringLiteral("看点") : QStringLiteral("完整"));
            highlightItem->setText(item.isHighlight ? item.listType : QStringLiteral("完整"));
            highlightItem->setFlags(highlightItem->flags() ^ Qt::ItemIsEditable);
            highlightItem->setTextAlignment(Qt::AlignCenter);
            ui.tableWidget_List->setItem(row, 2, highlightItem);
        }

        ++row;
    }

    ui.tableWidget_List->viewport()->update();
    ui.btn_select_all->setText(QStringLiteral("全选视频"));
    qInfo() << "视频列表渲染完成，显示" << VIDEOS.size() << "个视频";
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

    ui.btn_select_all->setText(allChecked ? "全选视频" : "取消选择");
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
        QMessageBox::warning(nullptr, "错误", "打开文件保存位置失败");
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

    QString savePath = readSavePath();
    int threadNum = readThreadNum();
    const QString quality = readQuality();
    const bool transcodeToMp4 = readTranscode();
    QList<int> selectedIndexes;
    QList<DownloadJob> jobs;

    auto appendJobForIndex = [&](int index) {
        auto it = VIDEOS.find(index);
        if (it == VIDEOS.end()) {
            qWarning() << "无效的video index:" << index;
            return false;
        }

        DownloadJob job;
        job.request.url = it->guid;
        job.request.videoTitle = it->title;
        job.request.quality = quality;
        job.request.savePath = savePath;
        job.request.threadCount = std::max(1, threadNum);
        job.request.transcodeToMp4 = transcodeToMp4;
        jobs.append(job);
        return true;
    };

    int rows = ui.tableWidget_List->rowCount();
    for (int r = 0; r < rows; ++r) {
        auto item = ui.tableWidget_List->item(r, 0);
        if (item && item->checkState() == Qt::Checked) {
            selectedIndexes.push_back(r);
        }
    }

    qInfo() << "选中" << selectedIndexes.size() << "个视频进行下载，保存路径:" << savePath << "线程数:" << threadNum;

    if (selectedIndexes.empty()) {
        qInfo() << "未选中批量下载，使用单个视频下载";
        const int selectedIndex = ui.tableWidget_List->currentRow();
        if (selectedIndex < 0 || !appendJobForIndex(selectedIndex)) {
            qWarning() << "下载失败: 未选择要下载的视频";
            QMessageBox::warning(this, "提示", "请先选择要下载的视频！");
            return;
        }
    } else {
        for (int index : selectedIndexes) {
            appendJobForIndex(index);
        }
    }

    if (jobs.isEmpty()) {
        qWarning() << "下载失败: 未生成任何下载任务";
        QMessageBox::warning(this, "提示", "未生成任何下载任务，请稍后重试。");
        return;
    }

    m_downloadProgressWindow->open();

    const bool started = jobs.size() == 1
        ? m_downloadCoordinator->startSingle(jobs.first())
        : m_downloadCoordinator->startBatch(jobs);
    if (!started && !m_downloadCoordinator->isBusy()) {
        QMessageBox::warning(this, "错误", "下载任务启动失败，请稍后重试。");
    }
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

void CCTVVideoDownloader::onCoordinatorBatchFinished(int completedJobs, int failedJobs, int cancelledJobs, int totalJobs, bool stoppedByFatalError)
{
    const QString prefix = stoppedByFatalError
        ? QStringLiteral("下载因严重错误停止：")
        : QStringLiteral("下载完成：");
    const QString message = QStringLiteral("%1已完成：%2，失败：%3，已取消：%4 / 总计：%5")
        .arg(prefix)
        .arg(completedJobs)
        .arg(failedJobs)
        .arg(cancelledJobs)
        .arg(totalJobs);
    statusBar()->showMessage(message, 8000);
    qInfo() << message;
}

void CCTVVideoDownloader::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updatePreviewImage();
}
