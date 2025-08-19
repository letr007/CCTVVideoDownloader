#include "../head/config.h"

std::unique_ptr<QSettings> g_settings;

extern void initGlobalSettings()
{
	bool configExists = QFile::exists("config/config.ini");
	g_settings = std::make_unique<QSettings>(
		"config/config.ini",
		QSettings::IniFormat
	);
	// 如果文件不存在就填充初始值
	if (!configExists)
	{
		g_settings->beginGroup("settings");
		g_settings->setValue("save_dir", "C:\\Video");
		g_settings->setValue("thread_num", 10);
		g_settings->setValue("transcode", false);
		g_settings->setValue("display_min", 1);
		g_settings->setValue("display_max", 100);
		g_settings->setValue("quality", 1);
		g_settings->endGroup();
		g_settings->beginGroup("programme");
		g_settings->endGroup();
		g_settings->sync();
	}
}

extern QList<QJsonObject> readProgrammeFromConfig()
{
	QList<QJsonObject> results;

	// 读取文件数据
	g_settings->sync();
	g_settings->beginGroup("programme");
	QStringList existingKeys = g_settings->childKeys();

	for (const QString& key : existingKeys)
	{
		QByteArray existingData = g_settings->value(key).toByteArray();
		// 解码base64
		QByteArray decodeData = QByteArray::fromBase64(existingData);
		if (decodeData.isEmpty())
		{
			qWarning() << "Base64解码失败 key:" << key;
			g_settings->endGroup();
			return QList<QJsonObject>();
		}
		// 转换为Json
		QJsonParseError error;
		QJsonDocument doc = QJsonDocument::fromJson(decodeData, &error);
		if (error.error != QJsonParseError::NoError)
		{
			qWarning() << "Json解析错误:" << error.errorString();
			g_settings->endGroup();
			return QList<QJsonObject>();
		}

		results.append(doc.object());
	}
	g_settings->endGroup();
	return results;
}

extern std::tuple<int, int> readDisplayMinAndMax()
{
	g_settings->sync();
	g_settings->beginGroup("settings");
	int displayMin = g_settings->value("display_min").toInt();
	int displayMax = g_settings->value("display_max").toInt();
	g_settings->endGroup();
	return std::make_tuple(displayMin, displayMax);
}

extern QString readQuality()
{
	g_settings->sync();
	g_settings->beginGroup("settings");
	QString quality = g_settings->value("quality").toString();
	g_settings->endGroup();
	return quality;
}

extern QString readSavePath()
{
	g_settings->sync();
	g_settings->beginGroup("settings");
	QString savePath = g_settings->value("save_dir").toString();
	g_settings->endGroup();
	return savePath;
}

extern int readThreadNum()
{
	g_settings->sync();
	g_settings->beginGroup("settings");
	int threadNum = g_settings->value("thread_num").toInt();
	g_settings->endGroup();
	return threadNum;
}