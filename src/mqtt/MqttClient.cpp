#include "mqtt/MqttClient.h"
#include "mqtt/MqttTopics.h"
#include "mqtt/MqttMessageBuilder.h"
#include "mqtt/MqttMessageParser.h"

#include <mosquitto.h>

#include <QDebug>
#include <QMetaObject>

namespace {
// libmosquitto_init is global, guard so multiple MqttClients don't double-init
std::atomic<int> g_mosq_refs{0};
} // namespace

MqttClient::MqttClient(const Config& cfg, QObject* parent)
    : QObject(parent), cfg_(cfg)
{
    if (g_mosq_refs.fetch_add(1) == 0)
        mosquitto_lib_init();

    mosq_ = mosquitto_new(cfg_.device.id.toUtf8().constData(),
                           /*clean_session=*/true,
                           /*userdata=*/this);
    if (!mosq_) {
        qCritical() << "[MQTT] mosquitto_new failed";
        return;
    }

    mosquitto_username_pw_set(mosq_,
        cfg_.mqtt.username.toUtf8().constData(),
        cfg_.mqtt.password.toUtf8().constData());

    // Last-will: broker re-publishes this (retained) if we drop the TCP
    // connection without a clean DISCONNECT. With keepalive_seconds=10 the
    // broker declares the device dead ~1.5 × keepalive after the last PING
    // reply, so the server sees an "lwt" disconnect inside ~15 s of a
    // real crash/power-loss — no periodic app-level ping needed.
    will_payload_ = MqttMessageBuilder::buildDisconnect(cfg_, "lwt");
    const QByteArray will_topic =
        mqtt_topics::disconnect(cfg_.device.id).toUtf8();
    mosquitto_will_set(mosq_, will_topic.constData(),
                       will_payload_.size(), will_payload_.constData(),
                       /*qos=*/1, /*retain=*/true);

    mosquitto_connect_callback_set(mosq_, &MqttClient::cbConnect);
    mosquitto_disconnect_callback_set(mosq_, &MqttClient::cbDisconnect);
    mosquitto_message_callback_set(mosq_, &MqttClient::cbMessage);
}

MqttClient::~MqttClient() {
    if (mosq_) {
        mosquitto_loop_stop(mosq_, /*force=*/true);
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
    }
    if (g_mosq_refs.fetch_sub(1) == 1)
        mosquitto_lib_cleanup();
}

bool MqttClient::isConnected() const { return connected_.load(); }

void MqttClient::connectToServer() {
    if (!mosq_) return;
    qInfo().nospace() << "[MQTT] connecting to "
                       << cfg_.mqtt.host << ":" << cfg_.mqtt.port;

    mosquitto_loop_start(mosq_);
    mosquitto_reconnect_delay_set(mosq_,
                                   /*delay=*/2,
                                   /*delay_max=*/30,
                                   /*exp backoff=*/true);

    const int rc = mosquitto_connect_async(mosq_,
        cfg_.mqtt.host.toUtf8().constData(),
        cfg_.mqtt.port,
        cfg_.mqtt.keepalive_seconds);
    if (rc != MOSQ_ERR_SUCCESS) {
        qWarning() << "[MQTT] connect_async failed:"
                    << mosquitto_strerror(rc)
                    << "— loop will retry";
        emit connectionError(QString::fromUtf8(mosquitto_strerror(rc)));
        // Do NOT return: the loop thread keeps retrying via the
        // reconnect_delay above, so the device recovers as soon as the
        // broker comes online.
    }
}

void MqttClient::disconnectGraceful() {
    if (!mosq_) return;
    if (connected_.load()) {
        QByteArray payload = MqttMessageBuilder::buildDisconnect(cfg_, "graceful");
        const QByteArray topic = mqtt_topics::disconnect(cfg_.device.id).toUtf8();
        mosquitto_publish(mosq_, nullptr, topic.constData(),
                           payload.size(), payload.constData(),
                           /*qos=*/1, /*retain=*/true);
    }
    mosquitto_disconnect(mosq_);
    mosquitto_loop_stop(mosq_, /*force=*/false);
}

// ── libmosquitto C callbacks → Qt main thread ────────────────────────────────

void MqttClient::cbConnect(mosquitto*, void* self, int rc) {
    auto* me = static_cast<MqttClient*>(self);
    QMetaObject::invokeMethod(me, "handleConnect",
                               Qt::QueuedConnection, Q_ARG(int, rc));
}

void MqttClient::cbDisconnect(mosquitto*, void* self, int rc) {
    auto* me = static_cast<MqttClient*>(self);
    QMetaObject::invokeMethod(me, "handleDisconnect",
                               Qt::QueuedConnection, Q_ARG(int, rc));
}

void MqttClient::cbMessage(mosquitto*, void* self,
                            const mosquitto_message* msg) {
    auto* me = static_cast<MqttClient*>(self);
    QByteArray topic   = QByteArray(msg->topic);
    QByteArray payload = QByteArray(
        static_cast<const char*>(msg->payload), msg->payloadlen);
    QMetaObject::invokeMethod(me, "handleInbound",
        Qt::QueuedConnection,
        Q_ARG(QByteArray, topic),
        Q_ARG(QByteArray, payload));
}

// ── Main-thread handlers ─────────────────────────────────────────────────────

void MqttClient::handleConnect(int rc) {
    if (rc == 0) {
        qInfo() << "[MQTT] connected";
        connected_.store(true);
        subscribe();
        publishConnect();
        emit connected();
    } else {
        qCritical() << "[MQTT] connection refused, code=" << rc
                    << mosquitto_connack_string(rc);
        emit connectionError(QString::fromUtf8(mosquitto_connack_string(rc)));
    }
}

void MqttClient::handleDisconnect(int rc) {
    qWarning() << "[MQTT] disconnected (rc=" << rc << ")";
    connected_.store(false);
    emit disconnected();
}

void MqttClient::handleInbound(QByteArray topic, QByteArray payload) {
    qDebug() << "[MQTT] RX" << topic;
    InboundMessage inb = MqttMessageParser::parse(payload);
    if (inb.type != InboundMsgType::Unknown)
        emit inboundMessage(inb);
    else
        qWarning() << "[MQTT] unknown inbound, payload:"
                   << payload.left(200);
}

// ── Subscription ────────────────────────────────────────────────────────────

void MqttClient::subscribe() {
    // Per-device command channel.
    const QByteArray cmd_topic = mqtt_topics::cmd(cfg_.device.id).toUtf8();
    int rc = mosquitto_subscribe(mosq_, nullptr, cmd_topic.constData(), 1);
    if (rc != MOSQ_ERR_SUCCESS)
        qCritical() << "[MQTT] subscribe failed:" << mosquitto_strerror(rc);
    else
        qInfo() << "[MQTT] subscribed to" << cmd_topic;

    // Location-wide broadcast channel (reconnect requests, fleet messages).
    const QByteArray bcast_topic = mqtt_topics::broadcast().toUtf8();
    rc = mosquitto_subscribe(mosq_, nullptr, bcast_topic.constData(), 1);
    if (rc != MOSQ_ERR_SUCCESS)
        qCritical() << "[MQTT] broadcast subscribe failed:"
                    << mosquitto_strerror(rc);
    else
        qInfo() << "[MQTT] subscribed to" << bcast_topic;
}

// ── Publishers ──────────────────────────────────────────────────────────────

void MqttClient::publishConnect() {
    if (!mosq_) return;
    QByteArray payload = MqttMessageBuilder::buildConnect(cfg_);
    QByteArray topic = mqtt_topics::connect(cfg_.device.id).toUtf8();
    mosquitto_publish(mosq_, nullptr, topic.constData(),
                       payload.size(), payload.constData(), 1, true);
    qInfo() << "[MQTT] TX connect";
}

void MqttClient::publishPayment(const PaymentMessage& msg) {
    if (!mosq_ || !connected_.load()) return;
    QByteArray payload = MqttMessageBuilder::buildPayment(msg);
    QByteArray topic = mqtt_topics::payment(cfg_.device.id, msg.barrier_id).toUtf8();
    mosquitto_publish(mosq_, nullptr, topic.constData(),
                       payload.size(), payload.constData(), 1, false);
    qInfo() << "[MQTT] TX payment" << msg.session_id;
}

void MqttClient::publishSessionAbandoned(const QString& session_id,
                                          const QString& barrier_id,
                                          const QString& plate,
                                          quint32 amount_due,
                                          quint32 amount_paid)
{
    if (!mosq_ || !connected_.load()) return;
    QByteArray payload = MqttMessageBuilder::buildSessionAbandoned(
        cfg_, session_id, barrier_id, plate, amount_due, amount_paid);
    QByteArray topic = mqtt_topics::event(cfg_.device.id).toUtf8();
    // QoS 1: the server MUST learn about an abandoned session — otherwise
    // it could hold the plate in a "pending payment" state forever.
    mosquitto_publish(mosq_, nullptr, topic.constData(),
                       payload.size(), payload.constData(), 1, false);
    qInfo() << "[MQTT] TX session_abandoned" << session_id;
}

void MqttClient::publishSupportRequest(const QString& session_id,
                                        const QString& barrier_id,
                                        const QString& plate,
                                        const QString& reason)
{
    if (!mosq_ || !connected_.load()) return;
    QByteArray payload = MqttMessageBuilder::buildSupportRequest(
        cfg_, session_id, barrier_id, plate, reason);
    QByteArray topic = mqtt_topics::event(cfg_.device.id).toUtf8();
    mosquitto_publish(mosq_, nullptr, topic.constData(),
                       payload.size(), payload.constData(), 1, false);
    qInfo() << "[MQTT] TX support_request session=" << session_id
            << "reason=" << reason;
}
