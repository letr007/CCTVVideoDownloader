#include "cli_controller.h"
#include "cli_logging.h"
#include "cli_parser.h"
#include "signal_bridge.h"

#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char* argv[])
{
    Cli::installQtMessageHandler();
    Cli::installFfmpegLogCallback();

    QCoreApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("cctv-video"));
    application.setApplicationVersion(QStringLiteral(CCTV_VIDEO_DOWNLOADER_BUILD_VERSION));

    const Cli::ParseResult parsed = Cli::parseArguments(application.arguments());
    Cli::setQtDebugLoggingEnabled(parsed.options.debug);
    qInfo() << "cctv-video started";
    if (parsed.shouldExit) {
        QTextStream stream(parsed.error.isEmpty() ? stdout : stderr);
        stream << (parsed.error.isEmpty() ? parsed.output : parsed.error) << Qt::endl;
        return parsed.exitCode;
    }

    Cli::Controller controller(application, parsed.options);
    Cli::SignalBridge signalBridge(application);
    QObject::connect(&signalBridge, &Cli::SignalBridge::interrupted, &controller, &Cli::Controller::cancel);
    controller.start();
    return application.exec();
}
