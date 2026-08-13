/**
 * main.cpp — Parking Barrier Client
 * Qt6 application for Orange Pi 4 LTS
 */

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QDebug>

#include <csignal>
#include <cstdio>
#include <unistd.h>

#include "app/Config.h"
#include "app/Application.h"
#include "app/DeviceId.h"
#include "app/Logger.h"

// ── Fatal signal handler ──────────────────────────────────────────────────────
// Qt's eglfs can raise SIGABRT if the DRM device becomes invalid unexpectedly.
// We log the signal and exit cleanly so systemd can restart the service,
// rather than producing a silent crash with no log entry.
static void fatalSignalHandler(int sig)
{
    // Avoid printf inside signal handler — write() is async-signal-safe.
    const char* msg = "[Main] fatal signal received — exiting for restart\n";
    (void)::write(STDERR_FILENO, msg, __builtin_strlen(msg));
    Logger::flush();
    // Reset to default and re-raise so the exit code reflects the signal.
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

// ── Display availability check ────────────────────────────────────────────────
// Peek at the DRM connector status before creating QApplication.
// Returns true if connected or if the status file doesn't exist (unknown).
static bool isDisplayConnected()
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    static const char* kPaths[] = {
        "/sys/class/drm/card0-HDMI-A-1/status",
        "/sys/class/drm/card0-HDMI-1/status",
        "/sys/class/drm/card1-HDMI-A-1/status",
        nullptr
    };
    for (int i = 0; kPaths[i]; ++i) {
        QFile f(kPaths[i]);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const bool connected = f.readLine().trimmed().toLower() == "connected";
        f.close();
        return connected;
    }
#endif
    return true;   // status file not found — assume connected
}

int main(int argc, char* argv[]) {
    // Install handlers before anything Qt touches so crashes are always logged.
    ::signal(SIGABRT, fatalSignalHandler);
    ::signal(SIGSEGV, fatalSignalHandler);

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        // linuxfb writes directly to /dev/fb0 and survives HDMI hot-plug
        // far better than eglfs (which holds a DRM master lease that breaks
        // on connector loss). Fall back to offscreen if fb0 is not available.
        if (QFile::exists("/dev/fb0"))
            qputenv("QT_QPA_PLATFORM", "linuxfb");
        else
            qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    if (!isDisplayConnected()) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        fprintf(stderr, "[Main] HDMI not connected at startup — using offscreen\n");
    }
#endif

    QApplication app(argc, argv);
    app.setApplicationName("ParkingClient");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("SmartPark");

    // CLI args
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({{"c", "config"},
                      "Path to config.json",
                      "config",
                      "/etc/parking_client/config.json"});
    parser.process(app);

    QString config_path = parser.value("config");

    // Fallback: look in current dir
    if (!QFile::exists(config_path))
        config_path = "config.json";

    // Start logger early with defaults so config-load errors also land in file.
    // Re-initialised below once the real config path is known.
    Logger::init({"/tmp/parking-client-bootstrap.log", 10LL * 1024 * 1024, 5, true});

    // Load config
    Config cfg;
    if (!cfg.load(config_path)) {
        qCritical() << "Failed to load config from" << config_path;
        return 1;
    }

    // Re-init with settings from config (shutdown is idempotent-safe)
    Logger::shutdown();
    Logger::init({
        cfg.logging.path,
        cfg.logging.max_size_bytes,
        cfg.logging.max_archives,
        cfg.logging.also_stderr,
    });

    // Auto-detect a stable device id from the board so deployments don't
    // need a hand-edited config value. The config `id` is used as a
    // fallback when auto-detection fails or is disabled.
    if (cfg.device.auto_id) {
        const QString detected = DeviceId::detect(cfg.device.id_prefix);
        if (!detected.isEmpty()) {
            if (!cfg.device.id.isEmpty() && cfg.device.id != detected) {
                qInfo() << "[Main] device.id" << cfg.device.id
                        << "overridden by detected id" << detected;
            }
            cfg.device.id = detected;
        } else {
            qWarning() << "[Main] device.id auto-detect failed — "
                          "falling back to config value" << cfg.device.id;
        }
    }
    if (cfg.device.id.isEmpty()) {
        qCritical() << "[Main] no device id available (auto-detect off and "
                       "config value empty)";
        return 1;
    }
    qInfo() << "[Main] device.id =" << cfg.device.id;

    // Compose full barrier IDs as "{device_id}_{suffix}" so they are
    // globally unique and clearly tied to their device.
    // Config stores only the short suffix (e.g. "01", "02").
    for (auto& bc : cfg.barriers)
        bc.id = cfg.device.id + "_" + bc.id;

    // Bind and expand vehicle-sensor barrier ids the same way. A sensor with
    // no explicit barrier_id (legacy singular `vehicle_sensor`) binds to the
    // first barrier so single-lane deployments keep working unchanged.
    for (auto& vs : cfg.vehicle_sensors) {
        QString suffix = vs.barrier_id;
        if (suffix.isEmpty() && !cfg.barriers.isEmpty())
            suffix = cfg.barriers.front().short_id;
        vs.barrier_id = suffix.isEmpty() ? QString()
                                         : cfg.device.id + "_" + suffix;
    }

    // Start application
    Application parking_app(cfg, config_path);
    parking_app.start();

    const int ret = app.exec();
    Logger::shutdown();
    return ret;
}
