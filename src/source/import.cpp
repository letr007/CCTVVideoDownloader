#include "../head/import.h"
#include "../head/apiservice.h"

Import::Import(QWidget* parent)
{
	ui.setupUi(this);
	this->setWindowIcon(QIcon(QPixmap(":/resources/cctvvideodownload.png")));
	ui.label->setText(QString("请输入视频播放页链接"));

	connect(ui.buttonBox, &QDialogButtonBox::accepted, this, &Import::ImportProgrammeFromUrl);
	connect(&APIService::instance(), &APIService::playColumnInfoResolved, this, &Import::handlePlayColumnInfoResolved);
	connect(&APIService::instance(), &APIService::playColumnInfoFailed, this, &Import::handlePlayColumnInfoFailed);
}

Import::~Import()
{
	emit importFinished();
}

void Import::ImportProgrammeFromUrl()
{
	setBusy(true);
	m_pendingPlayColumnInfoRequestId = APIService::instance().startGetPlayColumnInfo(ui.lineEdit->text());
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
		qWarning() << "获取数据失败";
		return;
	}

	const ProgrammePersistResult persisted = persistProgrammeImport(data);
	if (persisted.outcome == ProgrammePersistOutcome::Failed) {
		ui.label_status->setText(QStringLiteral("导入失败：无法保存节目信息。"));
		return;
	}

	qInfo() << (persisted.outcome == ProgrammePersistOutcome::Inserted ? "成功存储节目:"
		: persisted.outcome == ProgrammePersistOutcome::Upgraded ? "已升级节目:" : "节目已存在:")
		<< persisted.record.storageKey;
	accept();
}

void Import::handlePlayColumnInfoFailed(quint64 requestId, const QString& errorMessage)
{
	if (requestId != m_pendingPlayColumnInfoRequestId) {
		return;
	}

	setBusy(false);
	ui.label_status->setText(errorMessage.isEmpty() ? QStringLiteral("导入失败，请稍后重试。") : QStringLiteral("导入失败：%1").arg(errorMessage));
	qWarning() << errorMessage;
}

void Import::setBusy(bool busy)
{
	if (busy) {
		ui.label_status->setText(QStringLiteral("正在导入，请稍候..."));
	}
	ui.lineEdit->setEnabled(!busy);
	ui.buttonBox->setEnabled(!busy);
}

