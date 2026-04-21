/**
 * DisplayMonitor.h
 * Polls the DRM connector status file under /sys/class/drm/ so the application
 * can react to HDMI hot-plug events without requiring root or udev rules.
 *
 * Thread-safe: signals are emitted on the Qt main thread via a QTimer.
 *
 * On RK3399 (Orange Pi 4 LTS) the status file is typically one of:
 *   /sys/class/drm/card0-HDMI-A-1/status
 *   /sys/class/drm/card0-HDMI-1/status
 * The constructor probes both and picks the first one that exists.
 */

#pragma once

#include <QObject>
#include <QString>

class QTimer;

class DisplayMonitor : public QObject
{
    Q_OBJECT

public:
    explicit DisplayMonitor(QObject* parent = nullptr);

    // Start polling at the given interval (default 2 s).
    void start(int interval_ms = 2000);
    void stop();

    // Returns the current cached state (valid after start()).
    [[nodiscard]] bool isConnected() const { return connected_; }

    // Path of the DRM status file being watched, or empty if not found.
    [[nodiscard]] QString statusFilePath() const { return status_path_; }

signals:
    void displayConnected();
    void displayDisconnected();

private:
    void poll();
    static bool readConnected(const QString& path);

    QString status_path_;
    QTimer* timer_   = nullptr;
    bool    connected_ = true;   // assume connected until first poll
    bool    started_   = false;
};
