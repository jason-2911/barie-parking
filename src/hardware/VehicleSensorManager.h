/**
 * VehicleSensorManager.h
 * Qt-friendly wrapper around one IVehicleSensor per lane. Owns every concrete
 * driver declared in cfg.vehicle_sensors (one per barrier/lane) so the rest of
 * the app never touches platform headers, and exposes each lane's state as
 * signals tagged with the owning barrier id, delivered on the GUI thread.
 *
 * A `full` device drives two sensors (entry + exit); single-lane devices drive
 * one. The barrier id carried by every query and signal lets BarrierManager
 * route an event to the exact lane that produced it — no single-lane guessing.
 */

#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <vector>
#include "app/Config.h"
#include "hardware/IVehicleSensor.h"

class VehicleSensorManager : public QObject {
    Q_OBJECT
public:
    explicit VehicleSensorManager(const Config& cfg, QObject* parent = nullptr);
    ~VehicleSensorManager() override;

    void init();
    void shutdown();

    // Per-lane queries. `barrier_id` must match a sensor's bound barrier;
    // an unknown id reads as "not present" / no-op.
    [[nodiscard]] bool isPresent(const QString& barrier_id) const;
    void pollLive(const QString& barrier_id);   // synchronous hw re-read; GUI-thread safe

    // True if at least one lane's sensor is open.
    [[nodiscard]] bool isOpen() const;

signals:
    // Debounced, already-normalized transitions for one lane.
    // Always delivered on the thread that owns this QObject (GUI thread).
    void vehiclePresent(const QString& barrier_id);
    void vehicleCleared(const QString& barrier_id);

private:
    // Runs on a driver's worker thread — bounces through a queued invocation
    // onto this object's thread before the signal is emitted.
    Q_INVOKABLE void handleState(const QString& barrier_id, bool present);

    struct Lane {
        QString                         barrier_id;
        std::unique_ptr<IVehicleSensor> driver;
    };

    const Config&     cfg_;
    std::vector<Lane> lanes_;
};
