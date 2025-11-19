#include "../head/cctvvideodownloader.h"
#include "../head/about.h"
#include "../head/setting.h"
#include "../head/import.h"
#include "../head/downloaddialog.h"
#include "../head/concat.h"
#include "../head/decrypt.h"
#include "../head/apiservice.h"

//std::tuple<int, int> CCTVVideoDownloader::SELECTED_ID;

CCTVVideoDownloader::CCTVVideoDownloader(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);
    // 设置标题和图标
    setWindowTitle(QString("央视视频下载器"));
    setWindowIcon(QIcon(QPixmap(":/cctvvideodownload.png")));
    signalConnect();
    initGlobalSettings();
    flashProgrammeList();
    //g_apiservice = std::make_unique<APIService>(this);

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
    connect(ui.tableWidget_Config, &QTableWidget::cellClicked, this, &CCTVVideoDownloader::isProgrammeSelected); // 栏目表格点击
    connect(ui.flash_list, &QPushButton::clicked, this, &CCTVVideoDownloader::flashVideoList); // 刷新视频
    connect(ui.tableWidget_List, &QTableWidget::cellClicked, this, &CCTVVideoDownloader::isVideoSelected); // 刷新信息
    connect(ui.pushButton, &QPushButton::clicked, this, &CCTVVideoDownloader::openDownloadDialog); // 下载
}

void CCTVVideoDownloader::flashProgrammeList()
{
    // 读取配置文件
    QList<QJsonObject> programmes = readProgrammeFromConfig();

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
}

void CCTVVideoDownloader::flashVideoList()
{
    if (!SELECTED_ID) { qWarning() << "SELECTED_ID为空"; return; }
    auto [columnId, itemId] = *SELECTED_ID;
    auto [displayMin, displayMax] = readDisplayMinAndMax();
    VIDEOS = APIService::instance().getVideoList(
        columnId,
        itemId,
        displayMin,
        displayMax
    );
    // 显示结果
    ui.tableWidget_List->clearContents();
    ui.tableWidget_List->setRowCount(VIDEOS.size());
    ui.tableWidget_List->setColumnCount(2); // 设置为2列：复选框和标题

    // 设置列宽
    ui.tableWidget_List->setColumnWidth(0, 30);
    ui.tableWidget_List->setColumnWidth(1, 170);

    // 隐藏行头
    ui.tableWidget_List->verticalHeader()->setVisible(false);

    // 设置列标题
    ui.tableWidget_List->setHorizontalHeaderLabels({ "", "视频标题" });

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

        row++;
    }
    ui.tableWidget_List->viewport()->update();
}

void CCTVVideoDownloader::isProgrammeSelected(int r, int c)
{
    // 获取ID
    auto selectedItemColumnId = ui.tableWidget_Config->item(r, 1)->text();
    auto selectedItemItemId = ui.tableWidget_Config->item(r, 2)->text();
    // 获取名称
    auto selected_item_name = ui.tableWidget_Config->item(r, 0)->text();
    //qDebug() << "选中栏目:" << selected_item_name;
    SELECTED_ID.emplace(selectedItemColumnId, selectedItemItemId);
    //qDebug() << std::get<0>(SELECTED_ID.value()) << std::get<1>(SELECTED_ID.value());
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

	QList<QTableWidgetSelectionRange> ranges = ui.tableWidget_List->selectedRanges();
    if (ranges.size() > 0 && (QApplication::keyboardModifiers() & Qt::ControlModifier)) {
		for (const QTableWidgetSelectionRange& range : ranges) {
            for (int row = range.topRow(); row <= range.bottomRow(); ++row) {
                if (row == r) continue; // 跳过当前行，已处理
                auto item =  ui.tableWidget_List->item(row, 0);
                if (item->checkState() == Qt::Checked) {
                    item->setCheckState(Qt::Unchecked);
                } else {
                    item->setCheckState(Qt::Checked);
                }
            }
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
    DOWNLOAD_META_INFO.emplace(title, GUID);
    // 文本处理
    brief.replace(' ', '\n').replace('\r', '\n');
    brief.replace(QRegularExpression("\n+"), "\n");
    time.replace(' ', '\n');
    //qDebug() << brief;
    // 从API获取图片
    auto image = APIService::instance().getImage(imageUrl);
    if (!image.isNull())
    {
        ui.label_img->setPixmap(QPixmap::fromImage(image));
    }
    else
    {
        ui.label_img->setText("图片加载失败");
    }
    ui.label_title->setText(title);
    ui.label_introduce->setText(brief);
    ui.label_introduce->setWordWrap(true);
    ui.label_time->setText(time);
    
}

void CCTVVideoDownloader::openAboutDialog()
{
    About aboutDialog(this);
    aboutDialog.exec();
}

void CCTVVideoDownloader::openSettingDialog()
{
    Setting setttingDialog(this);
    setttingDialog.exec();
}

void CCTVVideoDownloader::openSaveDir()
{
    g_settings->beginGroup("settings");
    auto url = QUrl::fromLocalFile(g_settings->value("save_dir").toString());
    g_settings->endGroup();
    // 使用QDesktopServices打开路径
    if (!QDesktopServices::openUrl(url)) {
        qWarning() << "打开文件保存位置失败";
        QMessageBox::warning(nullptr, "Error", "打开文件保存位置失败");
    }

}

void CCTVVideoDownloader::openImportDialog()
{
    Import importDialog(this);
    connect(&importDialog, &Import::importFinished, this, &CCTVVideoDownloader::flashProgrammeList);
    importDialog.exec();
}

void CCTVVideoDownloader::openDownloadDialog()
{
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

    // 如果没有选中任何项，使用单个选中视频
    if (selectedIndexes.empty()) {
        if (!DOWNLOAD_META_INFO.has_value()) {
            QMessageBox::warning(this, "Warning", "请先选择要下载的视频！");
            return;
        }
        auto [title, GUID] = *DOWNLOAD_META_INFO;
        QStringList URLS = APIService::instance().getEncryptM3U8Urls(
            GUID,
            readQuality()
        );

        Download dialog(this);
        // 先关闭下载窗口再进行完成后操作
        connect(&dialog, &Download::DownloadFinished, this, [this, &dialog]() {
            dialog.accept();
            concatVideo();
            });
        dialog.transferDwonloadParams(title, URLS, savePath, threadNum);
        dialog.setModal(true);
        dialog.exec();
        return;
    }

    // 按选中顺序逐个处理
    for (int idx : selectedIndexes) {
        auto it = VIDEOS.find(idx);
        if (it == VIDEOS.end()) {
            qWarning() << "无效的video index(批量):" << idx;
            continue;
        }
        auto title = it->title;
        auto GUID = it->guid;

        // 设置当前下载元信息，便于后续 concat/decrypt 使用
        DOWNLOAD_META_INFO.emplace(title, GUID);

        QStringList URLS = APIService::instance().getEncryptM3U8Urls(
            GUID,
            readQuality()
        );

        Download dialog(this);
        connect(&dialog, &Download::DownloadFinished, this, [this, &dialog]() {
            dialog.accept();
            concatVideo();
            });
        dialog.transferDwonloadParams(title, URLS, savePath, threadNum);
        dialog.setModal(true);
        dialog.exec();
    }
}

void CCTVVideoDownloader::concatVideo()
{
    auto [title, GUID] = *DOWNLOAD_META_INFO;
    QString savePath = readSavePath();
    Concat concatDialog(this);
    connect(&concatDialog, &Concat::ConcatFinished, this, &CCTVVideoDownloader::decryptVideo);
    concatDialog.transferConcatParams(title, savePath);
    concatDialog.exec();
}

void CCTVVideoDownloader::decryptVideo()
{
    auto [title, GUID] = *DOWNLOAD_META_INFO;
    QString savePath = readSavePath();
    Decrypt decryptDialog(this);
    decryptDialog.transferDecryptParams(title, savePath);
    decryptDialog.exec();
}