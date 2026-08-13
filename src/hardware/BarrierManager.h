/**
 * BarrierManager.h
 * Orchestrator for every barrier declared in config.
 *
 * Wraps the low-level driver (IBarrierController) and runs a small,
 * well-defined state machine per barrier so the rest of the app only
 * sees high-level intents (requestOpen / requestClose).
 *
 * State machine (one per barrier):
 *
 *   Idle ──OPEN──▶ Raising ──▶ WaitingVehicle ──sensor:present──▶ Passing
 *                                   │                                │
 *                        safety-timeout                    sensor:clear
 *                                   │                                │
 *                                   ▼                                ▼
 *                                 ClearDelay ◀─────────────────  ClearDelay
 *                                   │   ▲
 *                          timeout  │   └── OPEN (reopen, cancel timer)
 *                                   ▼
 *                                Lowering ──OPEN──▶ Raising (reopen mid-close)
 *                                   │
 *                                   ▼
 *                                  Idle
 *
 * Guarantees:
 *   - OPEN arriving during Lowering aborts the close and re-raises.
 *   - Safety timeout fires a forced close if no vehicle ever appears,
 *     so a mis-triggered OPEN cannot leave the gate up forever.
 */

#pragma once

#include <QObject>
#include <QMap>
#include <QTimer>
#include <memory>
#include "app/Config.h"
#include "hardware/IBarrierController.h"

class VehicleSensorManager;

class BarrierManager : public QObject {
    Q_OBJECT
public:
    explicit BarrierManager(const Config& cfg,
                             VehicleSensorManager* sensor,
                             QObject* parent = nullptr);
    ~BarrierManager() override;

    // Returns true if the driver opened and at least one slave registered.
    // Application must check this and refuse to operate barriers if false.
    [[nodiscard]] bool init();
    void closeAll();

    // High-level requests — go through the state machine.
    void requestOpen(const QString& barrier_id, int auto_close_sec);
    void requestClose(const QString& barrier_id);

    bool    isOnline(const QString& barrier_id) const;
    QString position(const QString& barrier_id) const;

    // Exposes the low-level driver for provisioning operations (barrier
    // scan, Modbus-address rewrite). The driver's own mutex serialises
    // these against regular open/close traffic. Returns nullptr until
    // init() has run.
    IBarrierController* driver() const noexcept { return driver_.get(); }

signals:
    void statusChanged(const QString& barrier_id, const QString& position);
    void faultDetected(const QString& barrier_id, const QString& fault);

    // State-machine transitions — useful for UI and telemetry.
    void vehiclePassing(const QString& barrier_id);
    void vehiclePassed (const QString& barrier_id);
    void autoClosed    (const QString& barrier_id);

private slots:
    void pollStatus();
    // barrier_id identifies the lane whose sensor fired. Empty / unknown id
    // falls back to the single-lane heuristic (activeBarrierForSensor()).
    void onVehiclePresent(const QString& barrier_id);
    void onVehicleCleared(const QString& barrier_id);

private:
    enum class State {
        Idle,
        WaitingVehicle,   // barrier up, awaiting the vehicle to enter zone
        Passing,          // vehicle currently in the zone
        ClearDelay,       // vehicle cleared; countdown before lowering
        Lowering,         // close command issued; gate physically moving down
    };

    struct FsmSlot {
        State   state           = State::Idle;
        int     auto_close_sec  = 10;
        QTimer* clear_timer     = nullptr;   // clear-delay → lower
        QTimer* safety_timer    = nullptr;   // fallback if sensor never sees vehicle
    };

    // Edges
    void enterWaiting(const QString& barrier_id);
    void enterPassing(const QString& barrier_id);
    void enterClearDelay(const QString& barrier_id);
    void enterLowering(const QString& barrier_id);
    void enterIdle(const QString& barrier_id);
    void cancelTimers(FsmSlot& slot);
    void armSafety(const QString& barrier_id, FsmSlot& slot);

    // Mechanical I/O
    void rawRaise(const QString& barrier_id);
    void rawLower(const QString& barrier_id);

    // Fallback for events that arrive without a lane id (legacy single-lane).
    QString activeBarrierForSensor() const;

    // Resolve which barrier a sensor event refers to: prefer the explicit
    // lane id, fall back to the single-lane heuristic when it is empty/unknown.
    QString resolveSensorBarrier(const QString& barrier_id) const;

    // Per-lane clear delay (ms) before lowering. Falls back to 1500 ms when
    // no sensor is bound to the barrier.
    int clearDelayForBarrier(const QString& barrier_id) const;

    uint8_t modbusAddrFor(const QString& barrier_id) const;
    QString barrierIdFor(uint8_t modbus_addr) const;
    static QString positionToString(BarrierDriverPosition p);
    static const char* stateName(State s);

    const Config&                      cfg_;
    VehicleSensorManager*              sensor_ = nullptr;
    std::unique_ptr<IBarrierController> driver_;
    QTimer*                             poll_timer_ = nullptr;

    QMap<QString, QString>  positions_;
    QMap<QString, bool>     online_;
    QMap<QString, FsmSlot>  fsm_;
};
