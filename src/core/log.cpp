#include "log.h"

#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QMutex>
#include <QtCore/QThread>

namespace nimbus {

namespace {

LogLevel g_level = LogLevel::Info;
QMutex g_mutex;

const char* levelToStr(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::Trace: return "TRC";
        case LogLevel::Debug: return "DBG";
        case LogLevel::Info:  return "INF";
        case LogLevel::Warn:  return "WRN";
        case LogLevel::Error: return "ERR";
    }
    return "???";
}

LogLevel qtMsgTypeToLogLevel(QtMsgType type) {
    switch (type) {
        case QtDebugMsg: return LogLevel::Debug;
        case QtInfoMsg:  return LogLevel::Info;
        case QtWarningMsg: return LogLevel::Warn;
        case QtCriticalMsg:
        case QtFatalMsg: return LogLevel::Error;
        default: return LogLevel::Info;
    }
}

} // namespace

void setLogLevel(LogLevel level) {
    QMutexLocker lock(&g_mutex);
    g_level = level;
}

LogLevel logLevel() {
    QMutexLocker lock(&g_mutex);
    return g_level;
}

void installLogHandler(LogLevel level) {
    setLogLevel(level);
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
        LogLevel lvl = qtMsgTypeToLogLevel(type);
        if (lvl < logLevel()) return;

        QMutexLocker lock(&g_mutex);

        const QString timestamp = QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz");
        const QString file = ctx.file ? QString(ctx.file).section('/', -1) : "";
        const int line = ctx.line;
        const QString func = ctx.function ? QString(ctx.function) : "";
        const QString location = QString("%1:%2 %3").arg(file).arg(line, -4).arg(func);

        // Padded with leftJustified rather than a printf-style %-30s: QString::arg
        // does not understand width specifiers and would leave the literal behind.
        const QString out = QStringLiteral("%1 %2 %3 %4")
                                .arg(timestamp, levelToStr(lvl), location.leftJustified(30), msg);

        fprintf(stderr, "%s\n", qPrintable(out));
        fflush(stderr);
    });
}

void logMessage(LogLevel level, const QMessageLogContext& ctx, const QString& msg) {
    // Compatibility function for internal use
    if (level < logLevel()) return;

    QMutexLocker lock(&g_mutex);

    const QString timestamp = QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz");
    const QString file = ctx.file ? QString(ctx.file).section('/', -1) : "";
    const int line = ctx.line;
    const QString func = ctx.function ? QString(ctx.function) : "";
    const QString location = QString("%1:%2 %3").arg(file).arg(line, -4).arg(func);

    const QString out = QStringLiteral("%1 %2 %3 %4")
                            .arg(timestamp, levelToStr(level), location.leftJustified(30), msg);

    fprintf(stderr, "%s\n", qPrintable(out));
    fflush(stderr);
}

} // namespace nimbus