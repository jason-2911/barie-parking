// ═══════════════════════════════════════════════════════════════
// Config.cpp
// ═══════════════════════════════════════════════════════════════

#include "app/Config.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

#include <cerrno>
#include <cstdio>
#include <cstring>

bool Config::load(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qCritical() << "Cannot open config:" << path;
        return false;
    }

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qCritical() << "Config JSON error:" << err.errorString();
        return false;
    }

    auto root = doc.object();

    // device
    auto dev = root["device"].toObject();
    device.id             = dev["id"].toString();
    device.id_prefix      = dev["id_prefix"].toString();
    device.auto_id        = dev["auto_id"].toBool(true);
    device.description    = dev["description"].toString();
    // type: entry_only | exit_only | full. Default ExitOnly for back-compat.
    {
        QString ts = dev["type"].toString();
        if (!ts.isEmpty()) {
            device.type = deviceTypeFromStr(ts);
            if (device.type == DeviceType::Unknown) {
                qWarning() << "[Config] unknown device.type" << ts
                           << "— falling back to exit_only";
                device.type = DeviceType::ExitOnly;
            }
        }
    }
    device.has_cash_reader= dev["has_cash_reader"].toBool(
        device.type != DeviceType::EntryOnly);
    device.has_audio      = dev["has_audio"].toBool(true);

    // mqtt
    auto m = root["mqtt"].toObject();
    mqtt.host             = m["host"].toString();
    mqtt.port             = m["port"].toInt(1883);
    mqtt.username         = m["username"].toString();
    mqtt.password         = m["password"].toString();
    mqtt.keepalive_seconds= m["keepalive_seconds"].toInt(10);

    // serial
    auto s = root["serial"].toObject();
    serial.barrier_port = s["barrier_port"].toString("/dev/ttyUSB0");
    serial.barrier_baud = s["barrier_baud"].toInt(9600);
    serial.nv9_port     = s["nv9_port"].toString("/dev/ttyUSB1");
    serial.barrier_mock = s["barrier_mock"].toBool(false);

    // audio
    auto a = root["audio"].toObject();
    audio.sound_dir = a["sound_dir"].toString();
    audio.volume    = a["volume"].toInt(90);
    audio.device_default = a["device_default"].toString();
    audio.device_left    = a["device_left"].toString("parking_left");
    audio.device_right   = a["device_right"].toString("parking_right");
    auto smap = a["sounds"].toObject();
    for (auto it = smap.begin(); it != smap.end(); ++it)
        audio.sounds[it.key()] = it.value().toString();

    // logging
    auto lg = root["logging"].toObject();
    logging.path           = lg["path"].toString("/var/log/parking/parking.log");
    logging.max_size_bytes = static_cast<qint64>(
        lg["max_size_mb"].toInt(10)) * 1024 * 1024;
    logging.max_archives   = lg["max_archives"].toInt(5);
    logging.also_stderr    = lg["also_stderr"].toBool(true);

    // ui
    auto u = root["ui"].toObject();
    ui.fullscreen              = u["fullscreen"].toBool(true);
    ui.width                   = u["width"].toInt(1280);
    ui.height                  = u["height"].toInt(1024);
    // Accept legacy `idle_timeout_seconds` as a synonym so existing
    // deployments keep working — falls back to 300s (safety floor).
    const int legacy_idle = u["idle_timeout_seconds"].toInt(-1);
    ui.payment_timeout_seconds =
        u["payment_timeout_seconds"].toInt(legacy_idle > 0 ? legacy_idle : 300);
    auto st = root["self_test"].toObject();
    self_test.enabled                    = st["enabled"].toBool(false);
    self_test.auto_vehicle_sensor        = st["auto_vehicle_sensor"].toBool(false);
    self_test.vehicle_present_delay_ms   = st["vehicle_present_delay_ms"].toInt(1200);
    self_test.vehicle_clear_delay_ms     = st["vehicle_clear_delay_ms"].toInt(2500);
    self_test.auto_cash_insert_delay_ms  = st["auto_cash_insert_delay_ms"].toInt(1200);
    self_test.auto_call_support          = st["auto_call_support"].toBool(false);
    self_test.auto_call_support_delay_ms = st["auto_call_support_delay_ms"].toInt(1500);
    self_test.allow_short_payment_timeout=
        st["allow_short_payment_timeout"].toBool(false);

    if (ui.payment_timeout_seconds < 300 && !self_test.allow_short_payment_timeout) {
        qWarning() << "[Config] payment_timeout_seconds="
                   << ui.payment_timeout_seconds
                   << "is below safety minimum — forcing 300s";
        ui.payment_timeout_seconds = 300;
    }
    ui.payment_success_countdown_seconds =
        u["payment_success_countdown_seconds"].toInt(15);
    if (ui.payment_success_countdown_seconds < 5)
        ui.payment_success_countdown_seconds = 5;

    // vehicle sensors
    // Modern form is an array `vehicle_sensors[]`, one per lane, each tagged
    // with the `barrier_id` (short suffix) it serves. The legacy singular
    // `vehicle_sensor` object is still accepted and folded into a single
    // entry with no barrier_id, which main.cpp later binds to the first
    // barrier. main.cpp expands every barrier_id to "{device_id}_{suffix}".
    auto parseSensor = [](const QJsonObject& vs) {
        VehicleSensorConfig c;
        c.barrier_id     = vs["barrier_id"].toString();
        c.enabled        = vs["enabled"].toBool(true);
        c.chip_path      = vs["chip_path"].toString("/dev/gpiochip0");
        c.line_offset    = vs["line_offset"].toInt(17);
        c.active_low     = vs["active_low"].toBool(true);
        c.bias_pull_up   = vs["bias_pull_up"].toBool(true);
        c.debounce_us    = vs["debounce_us"].toInt(20000);
        c.clear_delay_ms = vs["clear_delay_ms"].toInt(1500);
        return c;
    };
    vehicle_sensors.clear();
    if (root["vehicle_sensors"].isArray()) {
        for (const auto& v : root["vehicle_sensors"].toArray())
            vehicle_sensors.append(parseSensor(v.toObject()));
    } else if (root.contains("vehicle_sensor")) {
        vehicle_sensors.append(parseSensor(root["vehicle_sensor"].toObject()));
    }

    // barriers
    barriers.clear();
    for (const auto& bv : root["barriers"].toArray()) {
        auto bo = bv.toObject();
        BarrierConfig bc;
        bc.short_id          = bo["id"].toString();
        bc.id                = bc.short_id;   // expanded in main.cpp after device.id is known
        bc.direction         = directionFromStr(bo["direction"].toString());
        bc.modbus_addr       = bo["modbus_addr"].toInt(1);
        bc.description       = bo["description"].toString();
        bc.auto_close_seconds= bo["auto_close_seconds"].toInt(10);
        bc.audio_channel     = audioChannelFromStr(bo["audio_channel"].toString());
        barriers.append(bc);
    }

    qInfo() << "Config loaded: device=" << device.id
            << "type=" << deviceTypeStr(device.type)
            << "barriers=" << barriers.size()
            << "sensors=" << vehicle_sensors.size();
    return true;
}

bool Config::save(const QString& path) const {
    // Re-read the original file so we preserve any keys the loader doesn't
    // touch (audio maps, logging paths, user tweaks, etc.). Only the fields
    // driven from provisioning are rewritten.
    QJsonObject root;
    QFile in(path);
    if (in.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(in.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            root = doc.object();
        }
        in.close();
    }

    // Patch device.type
    QJsonObject dev = root["device"].toObject();
    dev["type"]            = deviceTypeStr(device.type);
    dev["has_cash_reader"] = device.has_cash_reader;
    root["device"] = dev;

    // Patch barriers — always write short_id (the raw suffix) so that after
    // the next startup main.cpp can expand it again without double-prefixing.
    QJsonArray arr;
    for (const auto& b : barriers) {
        QJsonObject o;
        o["id"]                 = b.short_id.isEmpty() ? b.id : b.short_id;
        o["direction"]          = directionStr(b.direction);
        o["modbus_addr"]        = b.modbus_addr;
        o["description"]        = b.description;
        o["auto_close_seconds"] = b.auto_close_seconds;
        o["audio_channel"]      = audioChannelStr(b.audio_channel);
        arr.append(o);
    }
    root["barriers"] = arr;

    QDir().mkpath(QFileInfo(path).absolutePath());

    const QString tmp = path + ".tmp";
    QFile out(tmp);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[Config] save: cannot write" << tmp;
        return false;
    }
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    out.close();

    // Atomic replace via POSIX rename(2) — overwrites the destination
    // atomically on Linux. Do NOT use QFile::rename(): some Qt versions refuse
    // to overwrite an existing file, breaking the round-trip save/load test
    // and creating a brief window without a config file.
    if (::rename(tmp.toLocal8Bit().constData(),
                 path.toLocal8Bit().constData()) != 0) {
        qWarning() << "[Config] save: rename" << tmp << "→" << path
                   << "failed:" << strerror(errno);
        QFile::remove(tmp);   // clean up orphaned tmp
        return false;
    }
    qInfo() << "[Config] saved" << path;
    return true;
}
