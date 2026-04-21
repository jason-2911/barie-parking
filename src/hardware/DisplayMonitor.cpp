/**
 * DisplayMonitor.cpp — See DisplayMonitor.h
 */

#include "hardware/DisplayMonitor.h"

#include <QDebug>
#include <QFile>
#include <QStringList>
#include <QTimer>

// Candidate DRM status paths ordered by likelihood on RK3399-based boards.
static const QStringList kCandidates = {
    "/sys/class/drm/card0-HDMI-A-1/status",
    "/sys/class/drm/card0-HDMI-1/status",
    "/sys/class/drm/card1-HDMI-A-1/status",
    "/sys/class/drm/card1-HDMI-1/status",
};

DisplayMonitor::DisplayMonitor(QObject* parent)
    : QObject(parent)
{
    for (const QString& p : kCandidates) {
        if (QFile::exists(p)) {
            status_path_ = p;
            break;
        }
    }

    if (status_path_.isEmpty()) {
        qWarning() << "[DisplayMonitor] no DRM status file found — "
                      "hot-plug detection unavailable";
    } else {
        qInfo() << "[DisplayMonitor] watching" << status_path_;
    }

    timer_ = new QTimer(this);
    timer_->setSingleShot(false);
    connect(timer_, &QTimer::timeout, this, &DisplayMonitor::poll);
}

void DisplayMonitor::start(int interval_ms)
{
    if (started_) return;
    started_ = true;

    if (status_path_.isEmpty()) return;   // no status file — nothing to poll

    // Prime the initial state without emitting a spurious signal.
    connected_ = readConnected(status_path_);
    qInfo() << "[DisplayMonitor] initial state:"
            << (connected_ ? "connected" : "disconnected");

    timer_->start(interval_ms);
}

void DisplayMonitor::stop()
{
    timer_->stop();
    started_ = false;
}

void DisplayMonitor::poll()
{
    if (status_path_.isEmpty()) return;

    const bool now = readConnected(status_path_);
    if (now == connected_) return;

    connected_ = now;
    if (connected_) {
        qInfo() << "[DisplayMonitor] HDMI connected";
        emit displayConnected();
    } else {
        qWarning() << "[DisplayMonitor] HDMI disconnected";
        emit displayDisconnected();
    }
}

bool DisplayMonitor::readConnected(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QByteArray raw = f.readLine().trimmed().toLower();
    return raw == "connected";
}
