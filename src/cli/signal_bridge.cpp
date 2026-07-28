#include "signal_bridge.h"

#include <QCoreApplication>
#include <QTimer>

#include <csignal>

#ifdef Q_OS_WIN
#else
#include <QSocketNotifier>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

#ifdef Q_OS_WIN
volatile std::sig_atomic_t g_signalPending = 0;
#else
int g_signalPipe[2] = {-1, -1};
#endif

extern "C" void handleTermination(int)
{
#ifdef Q_OS_WIN
    g_signalPending = 1;
#else
    const char byte = 1;
    if (g_signalPipe[1] >= 0) {
        (void)::write(g_signalPipe[1], &byte, sizeof(byte));
    }
#endif
}

} // namespace

namespace Cli {

SignalBridge::SignalBridge(QCoreApplication& application)
    : QObject(&application)
{
#ifdef Q_OS_WIN
    m_timer = new QTimer(this);
    m_timer->setInterval(50);
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (g_signalPending != 0) {
            g_signalPending = 0;
            emit interrupted();
        }
    });
    m_timer->start();
#else
    if (::pipe(g_signalPipe) != 0) {
        return;
    }
    const int readFlags = ::fcntl(g_signalPipe[0], F_GETFL, 0);
    const int writeFlags = ::fcntl(g_signalPipe[1], F_GETFL, 0);
    if (readFlags < 0 || writeFlags < 0
        || ::fcntl(g_signalPipe[0], F_SETFL, readFlags | O_NONBLOCK) < 0
        || ::fcntl(g_signalPipe[1], F_SETFL, writeFlags | O_NONBLOCK) < 0) {
        ::close(g_signalPipe[0]);
        ::close(g_signalPipe[1]);
        g_signalPipe[0] = -1;
        g_signalPipe[1] = -1;
        return;
    }
    m_notifier = new QSocketNotifier(g_signalPipe[0], QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this] {
        char bytes[64];
        while (::read(g_signalPipe[0], bytes, sizeof(bytes)) > 0) {
        }
        emit interrupted();
    });
#endif
    std::signal(SIGINT, handleTermination);
#ifdef SIGTERM
    std::signal(SIGTERM, handleTermination);
#endif
}

SignalBridge::~SignalBridge()
{
    std::signal(SIGINT, SIG_DFL);
#ifdef SIGTERM
    std::signal(SIGTERM, SIG_DFL);
#endif
#ifndef Q_OS_WIN
    if (g_signalPipe[0] >= 0) {
        ::close(g_signalPipe[0]);
        g_signalPipe[0] = -1;
    }
    if (g_signalPipe[1] >= 0) {
        ::close(g_signalPipe[1]);
        g_signalPipe[1] = -1;
    }
#endif
}

} // namespace Cli
