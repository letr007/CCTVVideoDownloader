#pragma once

#include <QSettings>
#include <memory>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

extern std::unique_ptr<QSettings> g_settings;

extern QList<QJsonObject> readProgrammeFromConfig();
extern std::tuple<QString, QString> readDisplayMinAndMax();
extern QString readQuality();
extern QString readSavePath();
extern int readThreadNum();
extern int readLogLevel();

extern void initGlobalSettings();
