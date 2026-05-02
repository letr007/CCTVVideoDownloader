#include "../head/downloaddialog.h"
#include <QMessageBox>
#include <QCloseEvent>
#include <QtGlobal>
#include <algorithm>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <QResizeEvent>

Download::Download(QWidget* parent)
	: QDialog(parent)
{
	ui.setupUi(this);
	resize(700, 500);
	setMinimumSize(560, 400);
	setMaximumSize(840, 600);
	m_model = new DownloadModel(this);
	m_engine = new DownloadEngine(this);
	ui.tableView->setModel(m_model);
	// 设置列宽策略
	ui.tableView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	ui.tableView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	ui.tableView->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
	ui.tableView->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
	ui.tableView->setTextElideMode(Qt::ElideMiddle);
	ui.tableView->setWordWrap(false);
	layoutDownloadDialog();

	ui.progressBar_all->setValue(0);

	// 连接总进度更新
	connect(m_model, &DownloadModel::dataChanged, [this]() {
		ui.progressBar_all->setValue(m_model->totalProgress());
		});
}

void Download::resizeEvent(QResizeEvent* event)
{
	QDialog::resizeEvent(event);
	layoutDownloadDialog();
}

void Download::layoutDownloadDialog()
{
	if (!ui.label_7 || !ui.progressBar_all || !ui.tableView) {
		return;
	}

	const int margin = 12;
	const int gap = 8;
	const int labelHeight = 22;
	const int progressHeight = 24;
	const int tableY = margin + labelHeight + gap + progressHeight + gap;

	ui.label_7->setGeometry(margin, margin, width() - margin * 2, labelHeight);
	ui.progressBar_all->setGeometry(margin, margin + labelHeight + gap, width() - margin * 2, progressHeight);
	ui.tableView->setGeometry(margin, tableY, width() - margin * 2, height() - tableY - margin);

	ui.tableView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	ui.tableView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	ui.tableView->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
	ui.tableView->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
}

Download::~Download()
{
	persistShardState();
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
	m_initialThreadCount = std::max(1, threadNum);
	m_infoList.clear();
	m_failedIndexes.clear();
	m_downloadSuccessful = true;
	m_userCancelled = false;
	m_completionSignalScheduled = false;
	applyInitialDownloadPolicy();

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

	restorePersistedShardState();
	persistShardState();

	if (urls.isEmpty()) {
		m_downloadSuccessful = false;
		scheduleCompletionSignal(false);
		return;
	}

	if (pendingIndexesFromState().isEmpty()) {
		clearPersistedShardState();
		scheduleCompletionSignal(true);
		return;
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
		if (m_phase == DownloadPhase::Recovery) {
			info.url = QStringLiteral("补拉中: %1").arg(originalUrlForIndex(index));
		}
		if (progress >= 100)
		{
			info.status = DownloadStatus::Downloading;
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

	for (int index : pendingIndexesFromState())
	{
		m_engine->download(originalUrlForIndex(index), m_savePath, index);
	}
}

void Download::applyInitialDownloadPolicy()
{
	m_phase = DownloadPhase::Initial;
	m_engine->setMaxThreadCount(m_initialThreadCount);
	m_engine->setDefaultTimeoutMs(m_initialTimeoutMs);
	m_engine->setDefaultMaxAttempts(m_initialMaxAttempts);
	m_engine->setDefaultRetryDelayMs(m_initialRetryDelayMs);
}

QString Download::originalUrlForIndex(int index) const
{
	const int urlIndex = index - 1;
	if (urlIndex < 0 || urlIndex >= m_urls.size()) {
		return QString();
	}

	return m_urls.at(urlIndex);
}

QString Download::shardStateFilePath() const
{
	return QDir(m_savePath).filePath(".download_state.json");
}

bool Download::hasPersistedShardState() const
{
	return QFileInfo::exists(shardStateFilePath());
}

QString Download::shardFilePathForIndex(int index) const
{
	const QString shardUrl = originalUrlForIndex(index);
	if (shardUrl.isEmpty()) {
		return QString();
	}

	QUrl url(shardUrl);
	const QString fileName = QFileInfo(url.path()).fileName();
	if (fileName.isEmpty()) {
		return QString();
	}

	return QDir(m_savePath).filePath(fileName);
}

QList<int> Download::pendingIndexesFromState() const
{
	QList<int> pendingIndexes;
	for (auto it = m_infoList.cbegin(); it != m_infoList.cend(); ++it) {
		if (it.value().status != DownloadStatus::Finished) {
			pendingIndexes.append(it.key());
		}
	}

	std::sort(pendingIndexes.begin(), pendingIndexes.end());
	return pendingIndexes;
}

void Download::restorePersistedShardState()
{
	QSet<int> persistedPendingIndexes;
	QSet<int> persistedCompletedIndexes;
	bool persistedUrlsMatch = false;
	const bool stateExists = hasPersistedShardState();
	QFile stateFile(shardStateFilePath());
	if (stateFile.open(QIODevice::ReadOnly)) {
		const QJsonDocument document = QJsonDocument::fromJson(stateFile.readAll());
		const QJsonObject root = document.object();
		const QJsonArray urlsArray = root.value("urls").toArray();
		if (urlsArray.size() == m_urls.size()) {
			persistedUrlsMatch = true;
			for (int i = 0; i < urlsArray.size(); ++i) {
				if (urlsArray.at(i).toString() != m_urls.at(i)) {
					persistedUrlsMatch = false;
					break;
				}
			}
		}
		const QJsonArray pendingArray = root.value("pending_indexes").toArray();
		const QJsonArray completedArray = root.value("completed_indexes").toArray();
		for (const QJsonValue& value : pendingArray) {
			persistedPendingIndexes.insert(value.toInt());
		}
		for (const QJsonValue& value : completedArray) {
			persistedCompletedIndexes.insert(value.toInt());
		}
	}

	for (auto it = m_infoList.begin(); it != m_infoList.end(); ++it) {
		const int index = it.key();
		auto info = it.value();
		const QString filePath = shardFilePathForIndex(index);
		const QFileInfo fileInfo(filePath);
		const bool isPersistedComplete = stateExists
			&& persistedUrlsMatch
			&& persistedCompletedIndexes.contains(index)
			&& !persistedPendingIndexes.contains(index)
			&& fileInfo.exists()
			&& fileInfo.isFile()
			&& fileInfo.size() > 0;

		if (!isPersistedComplete) {
			info.status = DownloadStatus::Waiting;
			info.progress = 0;
			info.url = originalUrlForIndex(index);
		} else {
			info.status = DownloadStatus::Finished;
			info.progress = 100;
			info.url = originalUrlForIndex(index);
		}

		it.value() = info;
		m_model->updateInfo(info);
	}
}

void Download::persistShardState() const
{
	if (m_savePath.isEmpty()) {
		return;
	}

	QDir().mkpath(m_savePath);

	const QList<int> pendingIndexes = pendingIndexesFromState();
	if (pendingIndexes.isEmpty()) {
		clearPersistedShardState();
		return;
	}

	QJsonObject root;
	root.insert("version", 1);
	root.insert("phase", m_phase == DownloadPhase::Recovery ? "recovery" : "initial");

	QJsonArray urlsArray;
	for (const QString& url : m_urls) {
		urlsArray.append(url);
	}
	root.insert("urls", urlsArray);

	QJsonArray pendingArray;
	for (int index : pendingIndexes) {
		pendingArray.append(index);
	}
	root.insert("pending_indexes", pendingArray);

	QJsonArray completedArray;
	for (auto it = m_infoList.cbegin(); it != m_infoList.cend(); ++it) {
		if (it.value().status == DownloadStatus::Finished) {
			completedArray.append(it.key());
		}
	}
	root.insert("completed_indexes", completedArray);

	QSaveFile stateFile(shardStateFilePath());
	if (!stateFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return;
	}

	stateFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
	stateFile.commit();
}

void Download::clearPersistedShardState() const
{
	const QString stateFilePath = shardStateFilePath();
	if (QFileInfo::exists(stateFilePath)) {
		QFile::remove(stateFilePath);
	}
}

void Download::scheduleCompletionSignal(bool success)
{
	if (m_completionSignalScheduled) {
		return;
	}

	m_completionSignalScheduled = true;
	QTimer::singleShot(0, this, [this, success]() {
		m_completionSignalScheduled = false;
		emit DownloadFinished(success);
	});
}

void Download::startRecoveryDownloads()
{
	QList<int> recoveryIndexes = m_failedIndexes.values();
	if (recoveryIndexes.isEmpty()) {
		return;
	}

	std::sort(recoveryIndexes.begin(), recoveryIndexes.end());
	m_failedIndexes.clear();
	m_phase = DownloadPhase::Recovery;

	m_engine->setMaxThreadCount(1);
	m_engine->setDefaultTimeoutMs(m_recoveryTimeoutMs);
	m_engine->setDefaultMaxAttempts(m_recoveryMaxAttempts);
	m_engine->setDefaultRetryDelayMs(m_recoveryRetryDelayMs);

	for (int index : recoveryIndexes) {
		if (!m_infoList.contains(index)) {
			continue;
		}

		auto info = m_infoList[index];
		info.status = DownloadStatus::Waiting;
		info.progress = 0;
		info.url = QStringLiteral("补拉排队: %1").arg(originalUrlForIndex(index));
		m_infoList[index] = info;
		m_model->updateInfo(info);

		m_engine->download(originalUrlForIndex(index), m_savePath, index);
	}

	persistShardState();
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
			persistShardState();
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
	if (m_userCancelled) {
		persistShardState();
		emit DownloadFinished(false);
		return;
	}

	if (m_phase == DownloadPhase::Initial && !m_failedIndexes.isEmpty()) {
		persistShardState();
		startRecoveryDownloads();
		return;
	}

	persistShardState();
	if (m_downloadSuccessful && m_failedIndexes.isEmpty()) {
		clearPersistedShardState();
	}
	emit DownloadFinished(m_downloadSuccessful && m_failedIndexes.isEmpty());
}

void Download::onDownloadFinished(
	bool success,
	const QString& errorString,
	const QVariant& userData)
{
	if (!success)
	{
		auto index = userData.toInt();
		if (m_infoList.contains(index))
		{
			auto info = m_infoList[index];
			info.status = DownloadStatus::Error;
			m_failedIndexes.insert(index);
			if (m_phase == DownloadPhase::Recovery) {
				m_downloadSuccessful = false;
				info.url = QStringLiteral("补拉失败: %1").arg(errorString);
			}
			else {
				info.url = QStringLiteral("首轮失败: %1").arg(errorString);
			}
			m_infoList[index] = info;
			m_model->updateInfo(info);
		}
		persistShardState();
		return;
	}

	const auto index = userData.toInt();
	if (m_infoList.contains(index))
	{
		m_failedIndexes.remove(index);
		auto info = m_infoList[index];
		info.status = DownloadStatus::Finished;
		info.progress = 100;
		info.url = originalUrlForIndex(index);
		m_infoList[index] = info;
		m_model->updateInfo(info);
	}
	persistShardState();
}

#ifdef CORE_REGRESSION_TESTS
void Download::setTestReplyFactory(const std::function<QNetworkReply*(const QNetworkRequest&)>& replyFactory)
{
	m_engine->setTestReplyFactory(replyFactory);
}

void Download::clearTestReplyFactory()
{
	m_engine->clearTestReplyFactory();
}

void Download::setTestDownloadPolicies(int initialTimeoutMs,
	int initialMaxAttempts,
	int initialRetryDelayMs,
	int recoveryTimeoutMs,
	int recoveryMaxAttempts,
	int recoveryRetryDelayMs)
{
	m_initialTimeoutMs = initialTimeoutMs;
	m_initialMaxAttempts = initialMaxAttempts;
	m_initialRetryDelayMs = initialRetryDelayMs;
	m_recoveryTimeoutMs = recoveryTimeoutMs;
	m_recoveryMaxAttempts = recoveryMaxAttempts;
	m_recoveryRetryDelayMs = recoveryRetryDelayMs;
}
#endif
