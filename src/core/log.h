#pragma once

#include <QtCore/QMessageLogContext>
#include <QtCore/QMutex>
#include <QtCore/QString>

namespace nimbus {

enum class LogLevel {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
};

void installLogHandler(LogLevel level = LogLevel::Info);
void setLogLevel(LogLevel level);
LogLevel logLevel();

void logMessage(LogLevel level, const QMessageLogContext& ctx, const QString& msg);

} // namespace nimbus