#pragma once

#include "cli_support.h"

#include <QString>
#include <QStringList>

namespace Cli {

struct ParseResult {
    Options options;
    int exitCode = static_cast<int>(ExitCode::Success);
    QString output;
    QString error;
    bool shouldExit = false;
};

ParseResult parseArguments(const QStringList& arguments);

} // namespace Cli
