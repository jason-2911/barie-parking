/**
 * MqttClient.h
 * Thin Qt-friendly wrapper around libmosquitto.
 *
 * The mosquitto event loop runs on its own thread (`mosquitto_loop_start`);
 * messages are delivered back to the Qt main thread via queued signals,
 * so slots connected to `inboundMessage` / `connected` / `disconnected`
 * always run on the GUI thread.
 */

#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QTimer>
#include <QDateTime>
#include <atomic>

#include "app/Config.h"
#include "types/MessageTypes.h"

// Forward declare to avoid leaking libmosquitto headers through .h
struct mosquitto;
struct mosquitto_message;

class MqttClient : public QObject {
    Q_OBJECT
public:
    explicit MqttClient(const Config& cfg, QObject* parent = nullptr);
    ~MqttClient() override;

    void connectToServer();
    void disconnectGraceful();
    bool isConnected() const;

    // Outbound
    void publishConnect();
    void publishPing(const QVector<PingMessage::BarrierPingInfo>& barriers,
                     qint64 uptime_sec, int cpu_temp, int free_mem_mb);
    void publishPayment(const PaymentMessage& msg);
    void publishSessionAbandoned(const QString& session_id,
                                   const QString& barrier_id,
                                   const QString& plate,
                                   quint32 amount_due,
                                   quint32 amount_paid);
    void publishSupportRequest(const QString& session_id,
                                 const QString& barrier_id,
                                 const QString& plate,
                                 const QString& reason);

signals:
    void connected();
    void disconnected();
    void connectionError(const QString& reason);
    void inboundMessage(const InboundMessage& msg);
    void pingRequested();

private slots:
    void onPingTimer();

private:
    // libmosquitto C callbacks → bounce into methods below
    static void cbConnect(mosquitto*, void* self, int rc);
    static void cbDisconnect(mosquitto*, void* self, int rc);
    static void cbMessage(mosquitto*, void* self, const mosquitto_message*);

    // Called on main thread via QMetaObject::invokeMethod
    Q_INVOKABLE void handleConnect(int rc);
    Q_INVOKABLE void handleDisconnect(int rc);
    Q_INVOKABLE void handleInbound(QByteArray topic, QByteArray payload);

    void subscribe();
    void schedulePing();

    const Config&      cfg_;
    mosquitto*         mosq_ = nullptr;
    QTimer*            ping_timer_ = nullptr;
    std::atomic<bool>  connected_{false};
    QByteArray         will_payload_;  // must outlive mosquitto_will_set
};
