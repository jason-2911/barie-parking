/**
 * Config.h — Load and hold all runtime configuration from config.json
 */

#pragma once

#include <QString>
#include <QVector>
#include <QMap>
#include <QList>
#include "types/MessageTypes.h"

struct BarrierConfig {
    QString          id;
    BarrierDirection direction = BarrierDirection::Exit;
    int              modbus_addr = 1;
    QString          description;
    int              auto_close_seconds = 10;
};

struct VehicleSensorConfig {
    bool    enabled        = true;
    QString chip_path      = "/dev/gpiochip0";
    int     line_offset    = 17;     // BCM pin number
    bool    active_low     = true;   // NPN-NO pulls low when triggered
    bool    bias_pull_up   = true;   // enable internal pull-up
    int     debounce_us    = 20000;  // hardware-level debounce, 20 ms
    int     clear_delay_ms = 1500;   // wait this long after clear → close
};

struct Config {
    struct Device {
        QString    id;                 // may be overwritten at startup by DeviceId::detect()
        QString    id_prefix;          // prepended to the auto-detected id (e.g. "pk_")
        bool       auto_id  = true;    // if true, detect() runs and replaces `id`
        QString    description;
        // Deployment profile. Drives conditional init: entry_only is headless
        // (no NV9, no UI); exit_only and full bring up the full UI + NV9 stack.
        // If unspecified in config.json we fall back to ExitOnly for backward
        // compat with existing deployments.
        DeviceType type            = DeviceType::ExitOnly;
        bool       has_cash_reader = true;
        bool       has_audio       = true;
    } device;

    struct Mqtt {
        QString host;
        int     port = 1883;
        QString username;
        QString password;
        // MQTT keepalive + broker-side LWT replace the old app-level ping.
        // Broker declares the client dead ~1.5 × keepalive after the last
        // reply, so 10s ⇒ ≤15s detection latency. Raise this on lossy/
        // metered links to trade latency for bandwidth.
        int     keepalive_seconds = 10;
    } mqtt;

    struct Serial {
        QString barrier_port = "/dev/ttyUSB0";
        int     barrier_baud = 9600;
        QString nv9_port     = "/dev/ttyUSB1";
        // When true, HardwareFactory swaps the real Modbus driver for an
        // in-memory stub that only logs commands. Lets you run the full
        // app + UI on a board that has the cảm biến wired but no barrier
        // controller plugged in yet.
        bool    barrier_mock = false;
    } serial;

    struct Audio {
        QString sound_dir;
        int     volume = 90;
        QMap<QString,QString> sounds;

        QString soundPath(const QString& key) const {
            return sound_dir + "/" + sounds.value(key, key);
        }
    } audio;

    struct Logging {
        QString path          = "/var/log/parking/parking.log";
        qint64  max_size_bytes = 10LL * 1024 * 1024;   // 10 MB
        int     max_archives   = 5;
        bool    also_stderr    = true;
    } logging;

    struct Ui {
        bool fullscreen  = true;
        int  width       = 1280;
        int  height      = 1024;
        // How long the device waits for the user to finish an hourly
        // payment before publishing `session_abandoned` to the server
        // and asking the user to press the support button. 300s = 5 min
        // is the safety minimum — anything shorter is dangerous because
        // users commonly need time to find cash / open a banking app.
        int  payment_timeout_seconds = 300;
        // Fallback countdown on the payment-success screen when the server
        // does not override via `auto_idle_seconds`. A new vehicle_info
        // from the server pre-empts the countdown and switches the UI
        // immediately; this value is purely a safety floor so the screen
        // never hangs if the next event is delayed or lost.
        int  payment_success_countdown_seconds = 15;
    } ui;

    struct SelfTest {
        bool enabled = false;
        bool auto_vehicle_sensor = false;
        int  vehicle_present_delay_ms = 1200;
        int  vehicle_clear_delay_ms   = 2500;
        int  auto_cash_insert_delay_ms = 1200;
        bool auto_call_support = false;
        int  auto_call_support_delay_ms = 1500;
        bool allow_short_payment_timeout = false;
    } self_test;

    VehicleSensorConfig    vehicle_sensor;
    QVector<BarrierConfig> barriers;

    // Load from JSON file, returns false on error
    bool load(const QString& path);

    // Persist current state back to disk. Atomic (tmp + rename).
    // Only fields that are driven from wifi-provisioning are rewritten —
    // unknown fields in the file are preserved as-is.
    bool save(const QString& path) const;

    // Lookup barrier by id
    const BarrierConfig* findBarrier(const QString& id) const {
        for (const auto& b : barriers)
            if (b.id == id) return &b;
        return nullptr;
    }
};
