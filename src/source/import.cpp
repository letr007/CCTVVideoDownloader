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

void Import::handlePlayColumnInfoResolved(quint64 requestId, const QStringList& data)
{
	if (requestId != m_pendingPlayColumnInfoRequestId) {
		return;
	}

	setBusy(false);
	if (data.size() != 3 || data.at(0).isEmpty())
	{
		qWarning() << "获取数据失败";
		return;
	}

	// 构建JSON数据
	QJsonObject results{
		{"name", data.at(0)},
		{"itemid", data.at(1)},
		{"columnid", data.at(2)},
	};
	// 压缩编码为base64存储
	QByteArray jsonData = QJsonDocument(results).toJson(QJsonDocument::Compact);
	QString currentData = jsonData.toBase64();
	// 检查重复记录
	g_settings->sync();
	g_settings->beginGroup("programme");
	bool isDuplicate = false;
	const QStringList existingKeys = g_settings->childKeys();

	for (const QString& key : existingKeys)
	{
		QString existingData = g_settings->value(key).toString(); // 读取base64
		if (existingData == currentData)
		{
			isDuplicate = true;
			break;
		}
	}

	if (isDuplicate)
	{
		qInfo() << "跳过重复数据";
		g_settings->endGroup();
		return;
	}

	// 自增ID值
	int newId = 1;
	if (!existingKeys.isEmpty())
	{
		bool ok;
		int maxId = 0;
		for (const QString& key : existingKeys)
		{
			int currentId = key.toInt(&ok);
			if (ok && currentId > maxId)
			{
				maxId = currentId;
			}
		}
		newId = maxId + 1;
	}

	// 写入数据
	g_settings->setValue(QString::number(newId), currentData);
	g_settings->endGroup();
	g_settings->sync();

	qInfo() << "成功存储节目:" << newId;

	accept();
}

void Import::handlePlayColumnInfoFailed(quint64 requestId, const QString& errorMessage)
{
	if (requestId != m_pendingPlayColumnInfoRequestId) {
		return;
	}

	setBusy(false);
	qWarning() << errorMessage;
}

void Import::setBusy(bool busy)
{
	ui.lineEdit->setEnabled(!busy);
	ui.buttonBox->setEnabled(!busy);
}

