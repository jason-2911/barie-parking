/**
 * @file LocalBridge.cpp
 * @brief See LocalBridge.h
 */

#include "app/LocalBridge.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>

#include <mosquitto.h>

#include "app/Config.h"
#include "hardware/IBarrierController.h"

namespace {
constexpr const char* kTag = "[LocalBridge]";
} // namespace

LocalBridge::LocalBridge(const BridgeConfig& bcfg,
                          Config* cfg,
                          const QString& config_path,
                          QObject* parent)
    : QObject(parent)
    , bcfg_(bcfg)
    , cfg_(cfg)
    , config_path_(config_path)
{
    static std::atomic<bool> lib_inited{false};
    bool expected = false;
    if (lib_inited.compare_exchange_strong(expected, true)) {
        mosquitto_lib_init();
    }

    const QString client_id = "parking-local-" +
        QString::number(reinterpret_cast<quintptr>(this), 16);
    mosq_ = mosquitto_new(client_id.toUtf8().constData(), true, this);
}

LocalBridge::~LocalBridge() {
    stop();
    if (mosq_) {
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
    }
}

void LocalBridge::start() {
    if (!mosq_) return;

    if (!bcfg_.username.isEmpty()) {
        mosquitto_username_pw_set(mosq_,
            bcfg_.username.toUtf8().constData(),
            bcfg_.password.toUtf8().constData());
    }

    mosquitto_connect_callback_set(mosq_, &LocalBridge::cbConnect);
    mosquitto_message_callback_set(mosq_, &LocalBridge::cbMessage);

    int rc = mosquitto_connect(mosq_,
                                bcfg_.host.toUtf8().constData(),
                                bcfg_.port, 30);
    if (rc != MOSQ_ERR_SUCCESS) {
        qWarning() << kTag << "connect failed:" << mosquitto_strerror(rc)
                   << "— provisioning bridge disabled until next restart";
        return;
    }
    mosquitto_loop_start(mosq_);
    running_.store(true);
}

void LocalBridge::stop() {
    if (!running_.exchange(false)) return;
    if (mosq_) {
        mosquitto_disconnect(mosq_);
        mosquitto_loop_stop(mosq_, true);
    }
    connected_.store(false);
}

// ── libmosquitto callbacks ──────────────────────────────────────────────────

void LocalBridge::cbConnect(mosquitto* /*m*/, void* self, int rc) {
    auto* b = static_cast<LocalBridge*>(self);
    if (rc != 0) {
        qWarning() << kTag << "connect rc=" << rc
                   << mosquitto_connack_string(rc);
        return;
    }
    b->connected_.store(true);
    qInfo() << kTag << "connected" << b->bcfg_.host << ":" << b->bcfg_.port;
    mosquitto_subscribe(b->mosq_, nullptr,
                         b->bcfg_.request_topic.toUtf8().constData(), 1);
}

void LocalBridge::cbMessage(mosquitto* /*m*/, void* self,
                             const mosquitto_message* msg) {
    if (!msg || !msg->payload) return;
    auto* b = static_cast<LocalBridge*>(self);
    QByteArray payload(static_cast<const char*>(msg->payload),
                        msg->payloadlen);
    // Bounce to Qt main thread — handlers touch config/hardware which
    // assume single-threaded access.
    QMetaObject::invokeMethod(b, "handleRequest", Qt::QueuedConnection,
                               Q_ARG(QByteArray, payload));
}

// ── Request dispatch ────────────────────────────────────────────────────────

QByteArray LocalBridge::processEnvelope(const QByteArray& payload) {
    QJsonParseError jerr;
    auto doc = QJsonDocument::fromJson(payload, &jerr);
    if (jerr.error != QJsonParseError::NoError || !doc.isObject()) {
        return errJson("malformed_envelope", jerr.errorString());
    }
    auto env = doc.object();
    const QString action = env["action"].toString();
    if (action.isEmpty()) {
        return errJson("missing_action");
    }

    QByteArray params;
    if (env["params"].isObject()) {
        params = QJsonDocument(env["params"].toObject()).toJson(
            QJsonDocument::Compact);
    } else {
        params = "{}";
    }

    if (action == "scan_barriers")    return actionScanBarriers();
    if (action == "set_barrier_addr") return actionSetBarrierAddr(params);
    if (action == "get_config")       return actionGetConfig();
    if (action == "update_config")    return actionUpdateConfig(params);
    return errJson("unknown_action", action);
}

void LocalBridge::handleRequest(QByteArray payload) {
    QJsonParseError jerr;
    auto doc = QJsonDocument::fromJson(payload, &jerr);
    if (jerr.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << kTag << "malformed req:" << jerr.errorString();
        return;
    }
    const QString req_id = doc.object()["req_id"].toString();
    if (req_id.isEmpty()) {
        qWarning() << kTag << "missing req_id";
        return;
    }
    qInfo() << kTag << "req" << req_id
            << "action=" << doc.object()["action"].toString();

    publishAck(req_id, processEnvelope(payload));
}

void LocalBridge::publishAck(const QString& req_id, const QByteArray& payload) {
    if (!connected_.load()) return;
    const QString topic = bcfg_.ack_prefix + "/" + req_id;
    mosquitto_publish(mosq_, nullptr, topic.toUtf8().constData(),
                       payload.size(), payload.constData(), 1, false);
}

// ── Action handlers ─────────────────────────────────────────────────────────

QByteArray LocalBridge::actionScanBarriers() {
    if (!driver_) {
        return errJson("driver_unavailable",
                        "barrier driver not initialised");
    }
    QVector<uint8_t> found = driver_->scan(1, 32);
    QJsonArray arr;
    for (uint8_t a : found) {
        QJsonObject o;
        o["addr"]   = static_cast<int>(a);
        o["online"] = true;
        arr.append(o);
    }
    QJsonObject o;
    o["barriers"] = arr;
    return okJson(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QByteArray LocalBridge::actionSetBarrierAddr(const QByteArray& params) {
    if (!driver_) return errJson("driver_unavailable");
    auto p = QJsonDocument::fromJson(params).object();
    const int cur = p["current_addr"].toInt(0);
    const int neu = p["new_addr"].toInt(0);
    if (cur < 1 || cur > 247 || neu < 1 || neu > 247) {
        return errJson("bad_address", "current_addr/new_addr must be 1..247");
    }

    auto err = driver_->setSlaveAddress(static_cast<uint8_t>(cur),
                                         static_cast<uint8_t>(neu));
    if (err != BarrierDriverError::OK) {
        return errJson("modbus_write_failed",
                        QString("err=%1").arg(static_cast<int>(err)));
    }

    QJsonObject o;
    o["current_addr"] = cur;
    o["new_addr"]     = neu;
    return okJson(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QByteArray LocalBridge::actionGetConfig() {
    if (!cfg_) return errJson("no_config");

    QJsonArray arr;
    for (const auto& b : cfg_->barriers) {
        QJsonObject o;
        o["addr"] = b.modbus_addr;
        o["role"] = directionStr(b.direction);
        arr.append(o);
    }
    QJsonObject o;
    o["device_type"] = deviceTypeStr(cfg_->device.type);
    o["barriers"]    = arr;
    return okJson(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QByteArray LocalBridge::actionUpdateConfig(const QByteArray& params) {
    if (!cfg_) return errJson("no_config");
    auto p = QJsonDocument::fromJson(params).object();

    const QString dt_str = p["device_type"].toString();
    const DeviceType dt  = deviceTypeFromStr(dt_str);
    if (dt == DeviceType::Unknown) {
        return errJson("bad_device_type", dt_str);
    }

    QVector<BarrierConfig> new_barriers;
    for (const auto& v : p["barriers"].toArray()) {
        auto bo = v.toObject();
        BarrierConfig b;
        b.modbus_addr        = bo["addr"].toInt(0);
        b.direction          = directionFromStr(bo["role"].toString());
        b.id                 = QString("B%1").arg(b.modbus_addr, 2, 10, QChar('0'));
        b.description        = bo["description"].toString();
        b.auto_close_seconds = bo["auto_close_seconds"].toInt(10);
        if (b.modbus_addr < 1 || b.modbus_addr > 247) {
            return errJson("bad_barrier_addr",
                            QString::number(b.modbus_addr));
        }
        new_barriers.append(b);
    }

    // Minimum-barrier guard — catches a provisioning UI that forgot to
    // scan before saving.
    if (dt == DeviceType::Full    && new_barriers.size() < 2) return errJson("full_needs_2_barriers");
    if (dt == DeviceType::ExitOnly && new_barriers.isEmpty()) return errJson("exit_needs_1_barrier");
    if (dt == DeviceType::EntryOnly && new_barriers.isEmpty()) return errJson("entry_needs_1_barrier");

    cfg_->device.type = dt;
    cfg_->barriers    = new_barriers;
    cfg_->device.has_cash_reader = (dt != DeviceType::EntryOnly);

    if (!cfg_->save(config_path_)) {
        return errJson("save_failed", config_path_);
    }

    emit configUpdated();
    QJsonObject o;
    o["saved_to"] = config_path_;
    return okJson(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

// ── helpers ─────────────────────────────────────────────────────────────────

QByteArray LocalBridge::errJson(const QString& code, const QString& hint) {
    QJsonObject o;
    o["ok"]    = false;
    o["error"] = code;
    if (!hint.isEmpty()) o["hint"] = hint;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LocalBridge::okJson(const QByteArray& data) {
    QJsonObject o;
    o["ok"] = true;
    auto doc = QJsonDocument::fromJson(data);
    if (doc.isObject()) {
        auto inner = doc.object();
        for (auto it = inner.begin(); it != inner.end(); ++it) {
            o.insert(it.key(), it.value());
        }
    }
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
