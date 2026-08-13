/**
 * Application.h
 * Central coordinator: wires MQTT ↔ UI ↔ Hardware.
 */

#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QStringList>

#include "app/Config.h"
#include "app/CommandQueue.h"
#include "types/MessageTypes.h"

class MqttClient;
class BarrierManager;
class VehicleSensorManager;
class NV9Manager;
class AudioPlayer;
class MainWindow;
class DisplayMonitor;

class Application : public QObject {
    Q_OBJECT

public:
    explicit Application(Config& cfg,
                          const QString& config_path,
                          QObject* parent = nullptr);
    ~Application() override;

    void start();

private slots:
    // MQTT events
    void onMqttConnected();
    void onMqttDisconnected();
    void onMqttError(const QString& reason);
    void onInboundMessage(const InboundMessage& msg);

    // Hardware events
    void onBarrierStatusChanged(const QString& barrier_id, const QString& position);
    void onNV9CashInserted(quint32 amount);
    void onNV9Error(const QString& error);

    // UI events
    void onCallSupport();
    void onIdleTimeout();

    // Dispatched from CommandQueue once the previous command has ack'd.
    void runCommand(const CommandQueue::Command& cmd);

    // Barrier state-machine feedback → UI / session teardown
    void onBarrierAutoClosed(const QString& barrier_id);
    void onVehiclePassed(const QString& barrier_id);

    // Display hot-plug
    void onDisplayConnected();
    void onDisplayDisconnected();

private:
    void handleVehicleInfo(const VehicleInfo& vi);
    void handleOpenCommand(const OpenCommand& cmd);
    void handleQrConfirmed(const QrPaymentConfirmed& qr);
    void handlePaymentSuccess(const PaymentSuccess& ps);
    void handleDisplayMsg(const DisplayMessage& dm);

    void enqueueOpen(const QString& barrier_id, int auto_close_sec,
                      OpenReason reason = OpenReason::Unknown,
                      bool play_sound = true,
                      const QString& sound_file = {});

    // Called after the network/geolocation probe completes (or times out).
    void proceedStartup();

    Config&                cfg_;
    QString                config_path_;

    // True when the deployment profile includes a user-facing UI / cash
    // reader. EntryOnly runs headless — no Qt window, no NV9.
    bool                   has_ui_stack_ = true;

    MqttClient*            mqtt_     = nullptr;
    VehicleSensorManager*  sensor_   = nullptr;
    BarrierManager*        barrier_  = nullptr;
    NV9Manager*            nv9_      = nullptr;
    AudioPlayer*           audio_    = nullptr;
    MainWindow*            window_   = nullptr;
    CommandQueue*          cmd_queue_= nullptr;
    DisplayMonitor*        display_monitor_ = nullptr;

    bool                   display_available_ = true;

    QTimer*        payment_timer_ = nullptr;
    QElapsedTimer  uptime_;

    // Session state
    QString   current_session_id_;
    QString   current_barrier_id_;
    QString   current_plate_;
    QString   current_scenario_tag_;
    quint32   current_amount_due_   = 0;
    quint32    current_amount_paid_     = 0;
    QString    current_payment_method_;        // "Cash" | payment app from qr_payment_confirmed
    TicketType current_ticket_type_    = TicketType::Unknown;
    bool      session_active_       = false;
    // True once the payment timeout has fired and we have told the server
    // the session is abandoned. The session object is kept around so that a
    // ManualOverride OpenCommand (guard pressing emergency open) can still
    // close out the flow cleanly.
    bool      payment_stale_        = false;
};
