#pragma once
#ifndef LOGGER_H
#define LOGGER_H

#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QDateTime>

class Logger : public QObject
{
    Q_OBJECT

public:
    static Logger* instance();
    void init(const QString& logFilePath = "app.log");
    void cleanup();
    void setLogLevel(const int& level);
    bool shouldLog(QtMsgType type) const;

private:
    Logger(QObject* parent = nullptr);
    ~Logger();

    static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);
    void writeToFile(const QString& message);
    void outputToConsole(const QString& message);
    int logLevelToInt(const QString& level) const;

    QFile m_logFile;
    QTextStream m_textStream;
    QMutex m_mutex;
    static Logger* m_instance;
    int m_currentLogLevel; // 0: DEBUG, 1: INFO, 2: WARNING, 3: CRITICAL, 4: FATAL
};

#endif // LOGGER_H