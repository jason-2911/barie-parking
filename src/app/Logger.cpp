/**
 * Logger.cpp — See Logger.h
 */

#include "app/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QProcess>
#include <QTextStream>

// ─── Static members ───────────────────────────────────────────────────────────

Logger::Config                       Logger::config_;
std::unique_ptr<QFile>               Logger::file_;
std::unique_ptr<QTextStream>         Logger::stream_;
QMutex                               Logger::mutex_;
bool                                 Logger::initialized_ = false;

// ─── Public API ───────────────────────────────────────────────────────────────

void Logger::init(const Config& cfg)
{
    QMutexLocker lock(&mutex_);
    if (initialized_)
        return;

    config_ = cfg;

    if (config_.path.isEmpty())
        return;

    // Create parent directories if needed
    QFileInfo fi(config_.path);
    QDir().mkpath(fi.absolutePath());

    file_   = std::make_unique<QFile>(config_.path);
    stream_ = std::make_unique<QTextStream>();

    if (!file_->open(QIODevice::Append | QIODevice::Text)) {
        fprintf(stderr, "[Logger] Cannot open log file: %s\n",
                qPrintable(config_.path));
        file_.reset();
        stream_.reset();
        return;
    }

    stream_->setDevice(file_.get());
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    stream_->setEncoding(QStringConverter::Utf8);
#else
    stream_->setCodec("UTF-8");
#endif

    initialized_ = true;
    qInstallMessageHandler(qtMessageHandler);
}

void Logger::flush()
{
    QMutexLocker lock(&mutex_);
    if (stream_)
        stream_->flush();
}

void Logger::shutdown()
{
    // Remove handler first so no new writes arrive after we close the file
    qInstallMessageHandler(nullptr);

    QMutexLocker lock(&mutex_);
    if (stream_) {
        stream_->flush();
        stream_.reset();
    }
    if (file_) {
        file_->close();
        file_.reset();
    }
    initialized_ = false;
}

// ─── Qt message handler ───────────────────────────────────────────────────────

void Logger::qtMessageHandler(QtMsgType type,
                               const QMessageLogContext& /*ctx*/,
                               const QString& msg)
{
    const char* level = nullptr;
    switch (type) {
        case QtDebugMsg:    level = "DEBUG";    break;
        case QtInfoMsg:     level = "INFO ";    break;
        case QtWarningMsg:  level = "WARN ";    break;
        case QtCriticalMsg: level = "ERROR";    break;
        case QtFatalMsg:    level = "FATAL";    break;
    }

    const QString timestamp =
        QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    const QString line = QString("[%1] [%2] %3").arg(timestamp, level, msg);

    QMutexLocker lock(&mutex_);
    writeLocked(line);

    if (config_.also_stderr)
        fprintf(stderr, "%s\n", qPrintable(line));

    if (type == QtFatalMsg) {
        if (stream_) stream_->flush();
        abort();
    }
}

// ─── Private helpers (all called with mutex held) ────────────────────────────

void Logger::writeLocked(const QString& line)
{
    if (!stream_)
        return;

    *stream_ << line << '\n';
    stream_->flush();

    checkRotateLocked();
}

void Logger::checkRotateLocked()
{
    if (!file_)
        return;
    if (file_->size() >= config_.max_size_bytes)
        rotateLocked();
}

void Logger::rotateLocked()
{
    if (!file_)
        return;

    // Close the current log so we can rename it
    stream_->flush();
    stream_->setDevice(nullptr);
    file_->close();

    // Build archive name: parking.log → parking.20260420T153012Z.log
    const QFileInfo fi(config_.path);
    const QString stamp =
        QDateTime::currentDateTimeUtc().toString("yyyyMMddTHHmmssZ");
    const QString rotated = fi.absolutePath() + "/" +
                            fi.completeBaseName() + "." + stamp +
                            "." + fi.suffix();

    if (!QFile::rename(config_.path, rotated)) {
        fprintf(stderr, "[Logger] rotate: rename failed: %s → %s\n",
                qPrintable(config_.path), qPrintable(rotated));
        // Reopen original path and carry on
    } else {
        // Compress asynchronously — gzip -f replaces the plain file with .gz
        QProcess::startDetached("gzip", {"-f", rotated});
    }

    // Reopen a fresh log file at the original path
    file_->setFileName(config_.path);
    if (!file_->open(QIODevice::Append | QIODevice::Text)) {
        fprintf(stderr, "[Logger] rotate: cannot reopen %s\n",
                qPrintable(config_.path));
        file_.reset();
        stream_.reset();
        return;
    }
    stream_->setDevice(file_.get());

    pruneArchivesLocked();
}

void Logger::pruneArchivesLocked()
{
    const QFileInfo fi(config_.path);
    QDir dir(fi.absolutePath());

    // List all .gz archives whose base name starts with our log file stem
    const QString stem = fi.completeBaseName();
    QStringList archives = dir.entryList(
        QStringList() << (stem + ".*.log.gz") << (stem + ".*.gz"),
        QDir::Files,
        QDir::Name);   // ascending by name → oldest first (ISO timestamp)

    while (archives.size() > config_.max_archives) {
        const QString oldest = archives.takeFirst();
        if (!dir.remove(oldest))
            fprintf(stderr, "[Logger] prune: cannot remove %s\n",
                    qPrintable(oldest));
    }
}
