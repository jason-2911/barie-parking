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
    QString          id;           // full runtime id: "{device_id}_{short_id}"
    QString          short_id;     // raw id from config.json — used when saving
    BarrierDirection direction = BarrierDirection::Exit;
    int              modbus_addr = 1;
    QString          description;
    int              auto_close_seconds = 10;
    // Which speaker the open-chime for this barrier comes out of when the
    // device shares a single stereo audio jack between two barriers.
    // Default = system default device (no L/R routing).
    AudioChannel     audio_channel = AudioChannel::Default;
};

struct VehicleSensorConfig {
    // Which barrier (lane) this sensor serves. Stored as the short suffix
    // in config.json (e.g. "01"); expanded to the full "{device_id}_{suffix}"
    // at startup so it matches BarrierConfig::id. Empty in config means
    // "shared single-lane sensor" → bound to the first barrier at startup
    // (backward-compat with the legacy singular `vehicle_sensor` block).
    QString barrier_id;
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

        // ALSA device names used by the QProcess+aplay backend when routing
        // sound to a specific speaker. Empty `device_default` means "no -D
        // flag" → ALSA system default. The L/R defaults match the asoundrc
        // template shipped with the project.
        QString device_default;
        QString device_left  = "parking_left";
        QString device_right = "parking_right";

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

    // One sensor per lane/barrier. A device in `full` mode declares two
    // (one bound to the entry barrier, one to the exit barrier). Single-lane
    // devices declare one — or use the legacy singular `vehicle_sensor` block,
    // which the loader folds into a single entry here.
    QVector<VehicleSensorConfig> vehicle_sensors;
    QVector<BarrierConfig>       barriers;

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

    // Lookup the vehicle sensor bound to a given barrier id. Returns nullptr
    // if no sensor is configured for that lane.
    const VehicleSensorConfig* sensorForBarrier(const QString& barrier_id) const {
        for (const auto& s : vehicle_sensors)
            if (s.barrier_id == barrier_id) return &s;
        return nullptr;
    }
};
