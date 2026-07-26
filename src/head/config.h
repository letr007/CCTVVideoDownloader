#pragma once

#include <QSettings>
#include <memory>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPair>
#include "../parse/contentparse.h"

extern std::unique_ptr<QSettings> g_settings;

QString defaultSaveDirectory();
QString defaultConfigFilePath();

enum class ProgrammePersistOutcome {
    Inserted,
    Upgraded,
    Duplicate,
    Failed
};

struct ProgrammePersistResult {
    ProgrammePersistOutcome outcome = ProgrammePersistOutcome::Failed;
    ContentParse::ProgrammeRecord record;
};

QList<ContentParse::ProgrammeRecord> readProgrammeFromConfig();
ProgrammePersistResult persistProgrammeImport(const ContentParse::ImportResult& result);
extern std::tuple<QString, QString> readDisplayMinAndMax();
extern QString readQuality();
extern QString readSavePath();
extern int readThreadNum();
extern bool readTranscode();
extern int readLogLevel();
extern bool readShowHighlights();

extern void initGlobalSettings();
