/**
 * VehicleSensorManager.h
 * Qt-friendly wrapper around a single IVehicleSensor. Exposes the sensor
 * state as signals delivered on the GUI thread and owns the concrete
 * driver so the rest of the app never touches platform headers.
 */

#pragma once

#include <QObject>
#include <memory>
#include "app/Config.h"
#include "hardware/IVehicleSensor.h"

class VehicleSensorManager : public QObject {
    Q_OBJECT
public:
    explicit VehicleSensorManager(const Config& cfg, QObject* parent = nullptr);
    ~VehicleSensorManager() override;

    void init();
    void shutdown();

    [[nodiscard]] bool isPresent() const;
    [[nodiscard]] bool isOpen() const;

signals:
    // Debounced, already-normalized transitions.
    // Always delivered on the thread that owns this QObject (GUI thread).
    void vehiclePresent();
    void vehicleCleared();

private:
    // Runs on the driver's worker thread — bounces through a queued
    // invocation onto this object's thread.
    Q_INVOKABLE void handleState(bool present);

    const Config&                     cfg_;
    std::unique_ptr<IVehicleSensor>   driver_;
};
