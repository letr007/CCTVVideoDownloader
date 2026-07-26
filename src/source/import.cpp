#include "../head/import.h"
#include "../head/apiservice.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QStyle>

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
