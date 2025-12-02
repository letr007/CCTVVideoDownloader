#include "../head/config.h"

std::unique_ptr<QSettings> g_settings;

extern void initGlobalSettings()
{
	qInfo() << "初始化全局设置";
	
	bool configExists = QFile::exists("config/config.ini");
	qInfo() << "配置文件存在:" << configExists;
	
	g_settings = std::make_unique<QSettings>(
		"config/config.ini",
		QSettings::IniFormat
	);
	// 如果文件不存在就填充初始值
	if (!configExists)
	{
		qInfo() << "创建默认配置";
		g_settings->beginGroup("settings");
		g_settings->setValue("save_dir", "C:\\Video");
		g_settings->setValue("thread_num", 10);
		g_settings->setValue("transcode", false);
		g_settings->setValue("date_beg", QDate::currentDate().toString("yyyyMM"));
		g_settings->setValue("date_end", QDate::currentDate().addMonths(-1).toString("yyyyMM"));
		g_settings->setValue("quality", 1);
		g_settings->setValue("log_level", 1); // 默认日志级别为INFO
		g_settings->endGroup();
		g_settings->beginGroup("programme");
		g_settings->endGroup();
		g_settings->sync();
		qInfo() << "默认配置已创建并同步";
	} else {
		qInfo() << "使用现有配置文件";
	}
}

extern QList<QJsonObject> readProgrammeFromConfig()
{
	qInfo() << "从配置读取节目列表";
	
	QList<QJsonObject> results;

	// 读取文件数据
	g_settings->sync();
	g_settings->beginGroup("programme");
	QStringList existingKeys = g_settings->childKeys();
	
	qInfo() << "找到" << existingKeys.size() << "个节目配置项";

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
	
	qInfo() << "成功读取" << results.size() << "个节目配置";
	
	return results;
}

extern std::tuple<QString, QString> readDisplayMinAndMax()
{
	qInfo() << "读取显示日期范围配置";
	
	g_settings->sync();
	g_settings->beginGroup("settings");
	QString displayMin = g_settings->value("date_beg").toString();
	QString displayMax = g_settings->value("date_end").toString();
	g_settings->endGroup();
	
	qInfo() << "显示范围 - 最小值:" << displayMin << "最大值:" << displayMax;
	
	return std::make_tuple(displayMin, displayMax);
}

extern QString readQuality()
{
	qInfo() << "读取质量配置";
	
	g_settings->sync();
	g_settings->beginGroup("settings");
	QString quality = g_settings->value("quality").toString();
	g_settings->endGroup();
	
	qInfo() << "视频质量:" << quality;
	
	return quality;
}

extern QString readSavePath()
{
	qInfo() << "读取保存路径配置";
	
	g_settings->sync();
	g_settings->beginGroup("settings");
	QString savePath = g_settings->value("save_dir").toString();
	g_settings->endGroup();
	
	qInfo() << "保存路径:" << savePath;
	
	return savePath;
}

extern int readThreadNum()
{
	qInfo() << "读取线程数配置";
	
	g_settings->sync();
	g_settings->beginGroup("settings");
	int threadNum = g_settings->value("thread_num").toInt();
	g_settings->endGroup();
	
	qInfo() << "下载线程数:" << threadNum;
	
	return threadNum;
}

extern int readLogLevel()
{
	   qInfo() << "读取日志级别配置";
	   
	   g_settings->sync();
	   g_settings->beginGroup("settings");
	   int logLevel = g_settings->value("log_level").toInt();
	   g_settings->endGroup();
	   
	   qInfo() << "日志级别:" << logLevel;
	   
	   return logLevel;
}