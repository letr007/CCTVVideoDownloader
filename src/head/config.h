#pragma once

#include <QSettings>
#include <memory>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

extern std::unique_ptr<QSettings> g_settings;

extern QList<QJsonObject> readProgrammeFromConfig();
extern std::tuple<int, int> readDisplayMinAndMax();
extern QString readQuality();
extern QString readSavePath();
extern int readThreadNum();

extern void initGlobalSettings();
