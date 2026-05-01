#include "../head/downloaddialog.h"
#include <QMessageBox>
#include <QCloseEvent>
#include <QtGlobal>

Download::Download(QWidget* parent)
	: QDialog(parent)
{
	ui.setupUi(this);
	m_model = new DownloadModel(this);
	m_engine = new DownloadEngine(this);
	ui.tableView->setModel(m_model);
	// 设置列宽策略
	ui.tableView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	ui.tableView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	ui.tableView->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
	ui.tableView->horizontalHeader()->resizeSection(2, 180);
	ui.tableView->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

	ui.progressBar_all->setValue(0);

	// 连接总进度更新
	connect(m_model, &DownloadModel::dataChanged, [this]() {
		ui.progressBar_all->setValue(m_model->totalProgress());
		});
}

Download::~Download()
{
	disconnect(m_engine, nullptr, this, nullptr);
	if (m_engine) {
		if (m_engine->activeDownloads() > 0) {
			m_engine->setParent(nullptr);
			connect(m_engine, &DownloadEngine::allDownloadFinished, m_engine, &QObject::deleteLater);
			m_engine->cancelAll();
		} else {
			m_engine->waitForAllFinished();
			delete m_engine;
		}
		m_engine = nullptr;
	}
}

void Download::transferDwonloadParams(
	const QString& name,
	const QStringList& urls,
	const QString& savePath,
	const int& threadNum
)
{
	auto nameHash = QString(
		QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256)
		.toHex()
	);
	m_savePath = QDir::cleanPath(savePath + "/" + nameHash);
	m_urls = urls;
	m_engine->setMaxThreadCount(threadNum);

	// 初始化表格行
	m_model->setRowCount(urls.size());

	// 生成信息列表
	int index = 1;
	for (const auto& url : urls)
	{
		m_infoList.insert(index, { index, DownloadStatus::Waiting, url, 0 });
		m_model->updateInfo({ index, DownloadStatus::Waiting, url, 0 });
		++index;
	}

	// 启动下载
	stratDownload();
}

void Download::onDownloadProgress(
	qint64 bytesReceived,
	qint64 bytesTotal,
	const QVariant& userData
)
{
	double progress = bytesTotal > 0
		? 100.0 * static_cast<double>(bytesReceived) / static_cast<double>(bytesTotal)
		: 0.0;
	progress = qBound(0.0, progress, 100.0);
	auto index = userData.toInt();
	if (m_infoList.contains(index))
	{
		auto info = m_infoList[index];
		info.progress = static_cast<int>(progress);
		if (progress >= 100)
		{
			info.status = DownloadStatus::Finished;
		}
		else if (progress > 0)
		{
			info.status = DownloadStatus::Downloading;
		}
		m_infoList[index] = info;

		m_model->updateInfo(info);
	}
}

void Download::stratDownload()
{
	connect(m_engine, &DownloadEngine::downloadProgress, this, &Download::onDownloadProgress);
	connect(m_engine, &DownloadEngine::downloadFinished, this, &Download::onDownloadFinished);
	connect(m_engine, &DownloadEngine::allDownloadFinished, this, &Download::onAllDownloadFinished);

	for (const auto& info : m_infoList)
	{
		m_engine->download(info.url, m_savePath, info.index);
	}
}

void Download::closeEvent(QCloseEvent *event)
{
	if (m_engine && m_engine->activeDownloads() > 0)
	{
		QMessageBox::StandardButton reply;
		reply = QMessageBox::question(
			this,
			"确认退出",
			"有下载任务正在进行，确定要退出吗？",
			QMessageBox::Yes | QMessageBox::No);

		if (reply == QMessageBox::Yes)
		{
			m_userCancelled = true;
			m_downloadSuccessful = false;
			m_engine->cancelAll();
			setResult(QDialog::Rejected);
			event->accept();
		}
		else
		{
			event->ignore();
		}
	}
	else
	{
		event->accept();
	}
}

void Download::onAllDownloadFinished()
{
	//qDebug() << "下载完成";
	emit DownloadFinished(!m_userCancelled && m_downloadSuccessful);
}

void Download::onDownloadFinished(
	bool success,
	const QString& errorString,
	const QVariant& userData)
{
	if (!success)
	{
		m_downloadSuccessful = false;
		auto index = userData.toInt();
		if (m_infoList.contains(index))
		{
			auto info = m_infoList[index];
			info.status = DownloadStatus::Error;
			info.url = errorString;
			m_infoList[index] = info;
			m_model->updateInfo(info);
		}
	}
}
