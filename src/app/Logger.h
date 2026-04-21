/**
 * Logger.h — File-based rotating logger with gzip archiving.
 *
 * Routes Qt's qDebug/qInfo/qWarning/qCritical to a plain-text log file.
 * When the file exceeds max_size_bytes it is renamed with a UTC timestamp,
 * compressed with gzip, and the oldest archives are pruned when their count
 * exceeds max_archives.
 *
 * Thread-safe: all writes serialised through mutex_.
 */

#pragma once

#include <QMutex>
#include <QString>
#include <memory>

class QFile;
class QTextStream;

class Logger
{
public:
    struct Config {
        QString path;                                   // e.g. "/var/log/parking/parking.log"
        qint64  max_size_bytes = 10LL * 1024 * 1024;   // rotate when file exceeds this (default 10 MB)
        int     max_archives   = 5;                     // how many .gz files to keep
        bool    also_stderr    = true;                  // mirror output to stderr
    };

    // Install the Qt message handler and open the log file.
    // Call once, before any Qt logging.
    static void init(const Config& cfg);

    // Flush the underlying stream to disk.
    static void flush();

    // Flush, close the file and remove the Qt message handler.
    static void shutdown();

private:
    Logger() = delete;

    static void qtMessageHandler(QtMsgType type,
                                  const QMessageLogContext& ctx,
                                  const QString& msg);
    static void writeLocked(const QString& line);
    static void checkRotateLocked();
    static void rotateLocked();
    static void pruneArchivesLocked();

    static Config                       config_;
    static std::unique_ptr<QFile>       file_;
    static std::unique_ptr<QTextStream> stream_;
    static QMutex                       mutex_;
    static bool                         initialized_;
};
