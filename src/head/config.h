#pragma once

#include <QSettings>
#include <memory>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPair>

extern std::unique_ptr<QSettings> g_settings;

QString defaultSaveDirectory();
QString defaultConfigFilePath();

extern QList<QPair<QString, QJsonObject>> readProgrammeFromConfig();
extern std::tuple<QString, QString> readDisplayMinAndMax();
extern QString readQuality();
extern QString readSavePath();
extern int readThreadNum();
extern bool readTranscode();
extern int readLogLevel();
extern bool readShowHighlights();

extern void initGlobalSettings();
