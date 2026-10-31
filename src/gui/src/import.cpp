#include "../include/import.h"
#include "apiservice.h"

#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>

#include <optional>

namespace {

std::optional<ImportMode> g_testImportMode;

} // namespace

void setTestImportMode(ImportMode mode)
{
	g_testImportMode = mode;
}

void clearTestImportMode()
{
	g_testImportMode.reset();
}

ImportMode askImportMode(QWidget* parent, const ContentParse::ImportResult& result)
{
	if (g_testImportMode.has_value()) {
		return *g_testImportMode;
	}

	QMessageBox box(parent);
	box.setWindowTitle(QStringLiteral("导入单集视频"));
	box.setText(QStringLiteral("「%1」是单集播放页。").arg(result.title));
	box.setInformativeText(QStringLiteral(
		"下载本页视频会直接开始下载，不加入节目列表；解析视频列表可按栏目或专辑批量选择。"));
	QPushButton* downloadButton = box.addButton(QStringLiteral("下载本页视频"), QMessageBox::ActionRole);
	QPushButton* listButton = box.addButton(QStringLiteral("解析视频列表"), QMessageBox::AcceptRole);
	box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
	box.setDefaultButton(listButton);
	box.exec();

	if (box.clickedButton() == downloadButton) {
		return ImportMode::DownloadPageVideo;
	}
	if (box.clickedButton() == listButton) {
		return ImportMode::ListCatalogue;
	}
	return ImportMode::Cancelled;
}

Import::Import(QWidget* parent)
    : QDialog(parent)
{
	ui.setupUi(this);
	setWindowIcon(QIcon(QStringLiteral(":/cctvvideodownload.png")));
	auto* importButton = ui.buttonBox->button(QDialogButtonBox::Ok);
	auto* cancelButton = ui.buttonBox->button(QDialogButtonBox::Cancel);
	importButton->setText(QStringLiteral("导入"));
	importButton->setProperty("buttonRole", "primary");
	importButton->setFixedSize(88, 36);
	importButton->style()->unpolish(importButton);
	importButton->style()->polish(importButton);
	cancelButton->setText(QStringLiteral("取消"));
	cancelButton->setFixedSize(88, 36);

	connect(ui.buttonBox, &QDialogButtonBox::accepted, this, &Import::ImportProgrammeFromUrl);
	connect(ui.lineEdit, &QLineEdit::returnPressed, this, &Import::ImportProgrammeFromUrl);
	connect(&APIService::instance(), &APIService::playColumnInfoResolved, this, &Import::handlePlayColumnInfoResolved);
	connect(&APIService::instance(), &APIService::playColumnInfoFailed, this, &Import::handlePlayColumnInfoFailed);
}

Import::~Import()
{
}

void Import::ImportProgrammeFromUrl()
{
	const QString url = ui.lineEdit->text().trimmed();
	if (url.isEmpty()) {
		ui.label_status->setText(QStringLiteral("请输入视频播放页链接。"));
		ui.label_status->setProperty("severity", "danger");
		ui.label_status->style()->unpolish(ui.label_status);
		ui.label_status->style()->polish(ui.label_status);
		return;
	}

	setBusy(true);
	m_pendingPlayColumnInfoRequestId = APIService::instance().startGetPlayColumnInfo(url);
}

void Import::handlePlayColumnInfoResolved(quint64 requestId, const ContentParse::ImportResult& data)
{
	if (requestId != m_pendingPlayColumnInfoRequestId) {
		return;
	}

	setBusy(false);
	if (!data.isValid())
	{
		ui.label_status->setText(QStringLiteral("导入失败：未获取到有效节目信息。"));
		ui.label_status->setProperty("severity", "danger");
		ui.label_status->style()->unpolish(ui.label_status);
		ui.label_status->style()->polish(ui.label_status);
		qWarning() << "获取数据失败";
		return;
	}

	if (ContentParse::pageVideoOutsideCatalog(data))
	{
		const ImportMode mode = askImportMode(this, data);
		if (mode == ImportMode::Cancelled)
		{
			ui.label_status->setText(QStringLiteral("已取消导入。"));
			ui.label_status->setProperty("severity", "info");
			ui.label_status->style()->unpolish(ui.label_status);
			ui.label_status->style()->polish(ui.label_status);
			return;
		}
		if (mode == ImportMode::DownloadPageVideo)
		{
			emit pageVideoDownloadRequested(data.pageGuid, data.title);
			accept();
			return;
		}
	}

	const ProgrammePersistResult persisted = persistProgrammeImport(data);
	if (persisted.outcome == ProgrammePersistOutcome::Failed) {
		ui.label_status->setText(QStringLiteral("导入失败：无法保存节目信息。"));
		ui.label_status->setProperty("severity", "danger");
		ui.label_status->style()->unpolish(ui.label_status);
		ui.label_status->style()->polish(ui.label_status);
		return;
	}

	qInfo() << (persisted.outcome == ProgrammePersistOutcome::Inserted ? "成功存储节目:"
		: persisted.outcome == ProgrammePersistOutcome::Upgraded ? "已升级节目:" : "节目已存在:")
		<< persisted.record.storageKey;
	emit importFinished();
	accept();
}

void Import::handlePlayColumnInfoFailed(quint64 requestId, const QString& errorMessage)
{
	if (requestId != m_pendingPlayColumnInfoRequestId) {
		return;
	}

	setBusy(false);
	ui.label_status->setText(errorMessage.isEmpty() ? QStringLiteral("导入失败，请稍后重试。") : QStringLiteral("导入失败：%1").arg(errorMessage));
	ui.label_status->setProperty("severity", "danger");
	ui.label_status->style()->unpolish(ui.label_status);
	ui.label_status->style()->polish(ui.label_status);
	qWarning() << errorMessage;
}

void Import::setBusy(bool busy)
{
	if (busy) {
		ui.label_status->setText(QStringLiteral("正在导入，请稍候..."));
		ui.label_status->setProperty("severity", "info");
		ui.label_status->style()->unpolish(ui.label_status);
		ui.label_status->style()->polish(ui.label_status);
	}
	ui.lineEdit->setEnabled(!busy);
	ui.buttonBox->setEnabled(!busy);
}
