/**
 * @file LocalBridge.h
 * @brief Local MQTT IPC endpoint for the wifi-provisioning daemon.
 *
 * Subscribes to `parking/v1/local/req` on a dedicated mosquitto connection
 * to localhost. Handles four actions that were previously reachable only
 * via the cloud MQTT flow — all hardware/config mutation stays inside
 * parking-client so the provisioning UI never touches the serial bus or
 * config.json directly.
 *
 *  action             │ params                       │ ack payload
 *  ───────────────────┼──────────────────────────────┼──────────────────
 *  scan_barriers      │ {}                            │ { barriers:[{addr,online}] }
 *  set_barrier_addr   │ { current_addr, new_addr }   │ { ok|error }
 *  get_config         │ {}                            │ { device_type, barriers:[{addr,role}] }
 *  update_config      │ { device_type, barriers[…] } │ { ok|error }
 *
 * Thread-safe: mosquitto's network loop runs on its own thread;
 * incoming messages are dispatched back to the main (Qt) thread via
 * `QMetaObject::invokeMethod` so hardware calls don't race.
 */

#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <atomic>

struct mosquitto;
struct mosquitto_message;

struct Config;
class IBarrierController;

class LocalBridge : public QObject {
    Q_OBJECT
public:
    struct BridgeConfig {
        QString host          = "127.0.0.1";
        int     port          = 1883;
        QString username      = "parking";
        QString password      = "parking123";
        QString request_topic = "parking/v1/local/req";
        QString ack_prefix    = "parking/v1/local/ack";
    };

    explicit LocalBridge(const BridgeConfig& bcfg,
                          Config* cfg,
                          const QString& config_path,
                          QObject* parent = nullptr);
    ~LocalBridge() override;

    /// Plug the already-open barrier driver. LocalBridge borrows it — it
    /// does NOT own or close it. Call before `start()`.
    void setBarrierDriver(IBarrierController* drv) { driver_ = drv; }

    void start();
    void stop();

    /**
     * Synchronously process a request envelope and return the ack payload.
     * Exposed so the self-test harness can exercise every action without
     * spinning up a real mosquitto broker. Envelope shape is the same as
     * what arrives on the request topic:
     *   {"req_id":"…","action":"…","params":{…}}
     * Returns the payload that would have been published on the ack topic.
     */
    [[nodiscard]] QByteArray processEnvelope(const QByteArray& envelope);

signals:
    /// Emitted after a successful `update_config` action. Application
    /// listens so it can re-read config and reconfigure managers.
    void configUpdated();

private:
    // libmosquitto callback thunks
    static void cbConnect(mosquitto*, void* self, int rc);
    static void cbMessage(mosquitto*, void* self, const mosquitto_message*);

    Q_INVOKABLE void handleRequest(QByteArray payload);

    // Action handlers — called on the Qt main thread.
    QByteArray actionScanBarriers();
    QByteArray actionSetBarrierAddr(const QByteArray& params);
    QByteArray actionGetConfig();
    QByteArray actionUpdateConfig(const QByteArray& params);

    // Publishes an ack envelope to ${ack_prefix}/<req_id>.
    void publishAck(const QString& req_id, const QByteArray& payload);

    static QByteArray errJson(const QString& code, const QString& hint = {});
    static QByteArray okJson (const QByteArray& data = "{}");

    BridgeConfig        bcfg_;
    Config*             cfg_;
    QString             config_path_;
    IBarrierController* driver_ = nullptr;

    mosquitto*          mosq_ = nullptr;
    std::atomic<bool>   connected_{false};
    std::atomic<bool>   running_{false};
};
