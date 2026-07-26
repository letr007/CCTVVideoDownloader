#include "../head/config.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>

std::unique_ptr<QSettings> g_settings;

QString defaultSaveDirectory()
{
	QString directory = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
	if (directory.isEmpty()) {
		directory = QDir(QDir::homePath()).filePath(QStringLiteral("Videos"));
	}
	return QDir::cleanPath(directory);
}

QString defaultConfigFilePath()
{
	QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
	if (directory.isEmpty()) {
		directory = QDir(QDir::homePath()).filePath(QStringLiteral(".config/CCTVVideoDownloader"));
	}
	return QDir(directory).filePath(QStringLiteral("config.ini"));
}

extern void initGlobalSettings()
{
	qInfo() << "初始化全局设置";

	const QString configPath = defaultConfigFilePath();
	const QFileInfo configInfo(configPath);
	if (!QDir().mkpath(configInfo.absolutePath())) {
		qCritical() << "无法创建配置目录:" << configInfo.absolutePath();
	}

	const bool configExists = QFile::exists(configPath);
	qInfo() << "配置文件存在:" << configExists;

	g_settings = std::make_unique<QSettings>(configPath, QSettings::IniFormat);
	// 如果文件不存在就填充初始值
	if (!configExists)
	{
		qInfo() << "创建默认配置";
		g_settings->beginGroup("settings");
		g_settings->setValue("save_dir", defaultSaveDirectory());
		g_settings->setValue("thread_num", 1);
		g_settings->setValue("transcode", true);
		g_settings->setValue("date_beg", QDate::currentDate().toString("yyyyMM"));
		g_settings->setValue("date_end", QDate::currentDate().addMonths(-1).toString("yyyyMM"));
		g_settings->setValue("quality", 1);
		g_settings->setValue("log_level", 1); // 默认日志级别为INFO
		g_settings->setValue("show_highlights", false);
		g_settings->endGroup();
		g_settings->beginGroup("programme");
		g_settings->endGroup();
		g_settings->sync();
		qInfo() << "默认配置已创建并同步";
	} else {
		qInfo() << "使用现有配置文件";
	}
}

namespace {

constexpr int kProgrammeSchemaVersion = 2;

ContentParse::ProgrammeRecord decodeProgrammeRecord(const QString& key, const QByteArray& encoded)
{
	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromBase64(encoded), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		qWarning() << "节目配置解析失败 key:" << key << error.errorString();
		return {};
	}

	const QJsonObject object = document.object();
	ContentParse::ProgrammeRecord record;
	record.storageKey = key;
	record.title = object.value(QStringLiteral("name")).toString();
	record.itemId = object.value(QStringLiteral("itemid")).toString();
	record.columnId = object.value(QStringLiteral("columnid")).toString();
	record.catalogId = object.value(QStringLiteral("catalogid")).toString(record.columnId);
	record.profile = ContentParse::pageProfileFromName(object.value(QStringLiteral("profile")).toString());
	if (record.profile == ContentParse::PageProfile::Standard
		&& ContentParse::isVidE(record.itemId)
		&& ContentParse::isTopc(record.columnId)
		&& ContentParse::isHexGuid(record.catalogId)) {
		record.profile = ContentParse::PageProfile::LegacySportsEpisode;
	}
	return record;
}

QByteArray encodeProgrammeRecord(const ContentParse::ProgrammeRecord& record)
{
	const QJsonObject object{
		{QStringLiteral("name"), record.title},
		{QStringLiteral("itemid"), record.itemId},
		{QStringLiteral("columnid"), record.columnId},
		{QStringLiteral("catalogid"), record.catalogId},
		{QStringLiteral("profile"), ContentParse::pageProfileName(record.profile)},
		{QStringLiteral("version"), kProgrammeSchemaVersion},
	};
	return QJsonDocument(object).toJson(QJsonDocument::Compact).toBase64();
}

bool sameProgramme(const ContentParse::ProgrammeRecord& left, const ContentParse::ProgrammeRecord& right)
{
	return left.title == right.title
		&& left.itemId == right.itemId
		&& left.columnId == right.columnId
		&& left.catalogId == right.catalogId
		&& left.profile == right.profile;
}

} // namespace

QList<ContentParse::ProgrammeRecord> readProgrammeFromConfig()
{
	QList<ContentParse::ProgrammeRecord> records;
	if (!g_settings) {
		return records;
	}

	g_settings->sync();
	g_settings->beginGroup(QStringLiteral("programme"));
	for (const QString& key : g_settings->childKeys()) {
		ContentParse::ProgrammeRecord record = decodeProgrammeRecord(key, g_settings->value(key).toByteArray());
		if (record.isValid()) {
			records.append(record);
		}
	}
	g_settings->endGroup();
	return records;
}

ProgrammePersistResult persistProgrammeImport(const ContentParse::ImportResult& result)
{
	ProgrammePersistResult persisted;
	if (!g_settings || !result.isValid()) {
		return persisted;
	}

	ContentParse::ProgrammeRecord incoming = ContentParse::makeProgrammeRecord(result);
	if (!incoming.isValid()) {
		return persisted;
	}

	g_settings->sync();
	g_settings->beginGroup(QStringLiteral("programme"));
	const QStringList keys = g_settings->childKeys();
	int maxId = 0;
	ContentParse::ProgrammeRecord existing;
	for (const QString& key : keys) {
		bool numeric = false;
		maxId = qMax(maxId, key.toInt(&numeric));
		const ContentParse::ProgrammeRecord candidate = decodeProgrammeRecord(key, g_settings->value(key).toByteArray());
		const bool sameRawIdentity = candidate.itemId == incoming.itemId
			&& candidate.columnId == incoming.columnId;
		const bool legacyProjectedIdentity = incoming.profile == ContentParse::PageProfile::LegacySportsEpisode
			&& candidate.itemId == incoming.itemId
			&& candidate.columnId == incoming.catalogId;
		if (sameRawIdentity || legacyProjectedIdentity) {
			existing = candidate;
			break;
		}
	}

	if (!existing.storageKey.isEmpty() && sameProgramme(existing, incoming)) {
		g_settings->endGroup();
		persisted.outcome = ProgrammePersistOutcome::Duplicate;
		persisted.record = existing;
		return persisted;
	}

	incoming.storageKey = existing.storageKey.isEmpty() ? QString::number(maxId + 1) : existing.storageKey;
	g_settings->setValue(incoming.storageKey, encodeProgrammeRecord(incoming));
	g_settings->endGroup();
	g_settings->sync();
	persisted.outcome = existing.storageKey.isEmpty()
		? ProgrammePersistOutcome::Inserted : ProgrammePersistOutcome::Upgraded;
	persisted.record = incoming;
	return persisted;
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

extern void writeDisplayMinAndMax(const QString& displayMin, const QString& displayMax)
{
	g_settings->beginGroup(QStringLiteral("settings"));
	g_settings->setValue(QStringLiteral("date_beg"), displayMin);
	g_settings->setValue(QStringLiteral("date_end"), displayMax);
	g_settings->endGroup();
	g_settings->sync();
}

extern std::tuple<QString, QString> normalizeDisplayMonths(
	const QString& displayMin,
	const QString& displayMax,
	const QDate& latestDate)
{
	const QDate earliestMonth(2000, 1, 1);
	const QDate latestMonth(latestDate.year(), latestDate.month(), 1);
	auto parseMonth = [&](const QString& value, const QDate& fallback) {
		const QDate parsed = QDate::fromString(value + QStringLiteral("01"),
			QStringLiteral("yyyyMMdd"));
		return qMax(earliestMonth, parsed.isValid() ? parsed : fallback);
	};

	QDate startMonth = parseMonth(displayMin, latestMonth);
	QDate endMonth = parseMonth(displayMax, latestMonth.addMonths(-1));
	if (startMonth < endMonth) {
		std::swap(startMonth, endMonth);
	}
	if (startMonth > latestMonth) {
		const int monthSpan = (startMonth.year() - endMonth.year()) * 12
			+ startMonth.month() - endMonth.month();
		startMonth = latestMonth;
		endMonth = qMax(earliestMonth, latestMonth.addMonths(-monthSpan));
	}

	return {
		startMonth.toString(QStringLiteral("yyyyMM")),
		endMonth.toString(QStringLiteral("yyyyMM"))
	};
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

extern bool readTranscode()
{
	qInfo() << "读取封装格式配置";

	g_settings->sync();
	g_settings->beginGroup("settings");
	bool transcode = g_settings->value("transcode", true).toBool();
	g_settings->endGroup();

	qInfo() << "封装为MP4:" << transcode;

	return transcode;
}

extern bool readShowHighlights()
{
	qInfo() << "读取看点列表显示配置";

	g_settings->sync();
	g_settings->beginGroup("settings");
	bool showHighlights = g_settings->value("show_highlights", false).toBool();
	g_settings->endGroup();

	qInfo() << "显示看点列表:" << showHighlights;

	return showHighlights;
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
