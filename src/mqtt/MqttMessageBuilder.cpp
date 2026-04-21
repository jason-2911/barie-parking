#include "mqtt/MqttMessageBuilder.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

QByteArray MqttMessageBuilder::buildConnect(const Config& cfg) {
    QJsonArray barriers_arr;
    for (const auto& b : cfg.barriers) {
        QJsonObject o;
        o["barrier_id"]  = b.id;
        o["direction"]   = directionStr(b.direction);
        o["modbus_addr"] = b.modbus_addr;
        o["description"] = b.description;
        barriers_arr.append(o);
    }

    QJsonObject hw;
    hw["model"]           = "Orange Pi 4 LTS";
    hw["soc"]             = "RK3399";
    hw["firmware"]        = "1.0.0";
    hw["has_cash_reader"] = cfg.device.has_cash_reader;
    hw["has_display"]     = true;
    hw["has_audio"]       = cfg.device.has_audio;

    QJsonObject root;
    root["type"]        = "connect";
    root["device_id"]   = cfg.device.id;
    root["timestamp"]   = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["barriers"]    = barriers_arr;
    root["hw_info"]     = hw;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray MqttMessageBuilder::buildDisconnect(const Config& cfg,
                                                const QString& reason) {
    QJsonObject root;
    root["type"]      = "disconnect";
    root["device_id"] = cfg.device.id;
    root["reason"]    = reason;
    root["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray MqttMessageBuilder::buildPing(
    const Config& cfg,
    const QVector<PingMessage::BarrierPingInfo>& barriers,
    qint64 uptime_seconds, int cpu_temp_c, int free_mem_mb) {

    QJsonArray barr_arr;
    for (const auto& b : barriers) {
        QJsonObject o;
        o["barrier_id"] = b.id;
        o["direction"]  = directionStr(b.direction);
        o["status"]     = b.status;
        o["position"]   = b.position;
        barr_arr.append(o);
    }

    QJsonObject sys;
    sys["cpu_temp_c"]     = cpu_temp_c;
    sys["free_mem_mb"]    = free_mem_mb;
    sys["cash_reader_ok"] = true;

    QJsonObject root;
    root["type"]           = "ping";
    root["device_id"]      = cfg.device.id;
    root["timestamp"]      = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["uptime_seconds"] = uptime_seconds;
    root["barriers"]       = barr_arr;
    root["system"]         = sys;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray MqttMessageBuilder::buildPayment(const PaymentMessage& msg) {
    QJsonObject root;
    root["type"]          = "payment";
    root["device_id"]     = msg.device_id;
    root["barrier_id"]    = msg.barrier_id;
    root["session_id"]    = msg.session_id;
    root["plate"]         = msg.plate;
    root["method"]        = "cash";
    root["amount_due"]    = static_cast<qint64>(msg.amount_due);
    root["amount_paid"]   = static_cast<qint64>(msg.amount_paid);
    root["amount_change"] = static_cast<qint64>(msg.amount_change);
    root["timestamp"]     = QDateTime::currentDateTime().toString(Qt::ISODate);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray MqttMessageBuilder::buildSessionAbandoned(
        const Config& cfg, const QString& session_id,
        const QString& barrier_id, const QString& plate,
        quint32 amount_due, quint32 amount_paid)
{
    QJsonObject root;
    root["type"]        = "session_abandoned";
    root["device_id"]   = cfg.device.id;
    root["barrier_id"]  = barrier_id;
    root["session_id"]  = session_id;
    root["plate"]       = plate;
    root["amount_due"]  = static_cast<qint64>(amount_due);
    root["amount_paid"] = static_cast<qint64>(amount_paid);
    root["timestamp"]   = QDateTime::currentDateTime().toString(Qt::ISODate);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray MqttMessageBuilder::buildSupportRequest(
        const Config& cfg, const QString& session_id,
        const QString& barrier_id, const QString& plate,
        const QString& reason)
{
    QJsonObject root;
    root["type"]        = "support_request";
    root["device_id"]   = cfg.device.id;
    root["barrier_id"]  = barrier_id;
    root["session_id"]  = session_id;
    root["plate"]       = plate;
    root["reason"]      = reason;
    root["timestamp"]   = QDateTime::currentDateTime().toString(Qt::ISODate);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
