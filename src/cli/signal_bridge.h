#pragma once

#include <QObject>

class QCoreApplication;
class QTimer;
#ifndef Q_OS_WIN
class QSocketNotifier;
#endif

namespace Cli {

class SignalBridge final : public QObject
{
    Q_OBJECT

public:
    explicit SignalBridge(QCoreApplication& application);
    ~SignalBridge() override;

signals:
    void interrupted();

private:
#ifdef Q_OS_WIN
    QTimer* m_timer = nullptr;
#else
    QSocketNotifier* m_notifier = nullptr;
#endif
};

} // namespace Cli
