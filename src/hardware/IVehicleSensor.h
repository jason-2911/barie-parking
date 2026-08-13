/**
 * IVehicleSensor.h
 * Abstract interface for vehicle presence sensors (IR proximity,
 * photoelectric, inductive loop, etc.).
 *
 * Concrete implementations:
 *   - VehicleSensorGpiod (E3F-DS10C4 IR sensor via libgpiod on Linux)
 *   - (future) VehicleSensorLoop, VehicleSensorCamera, ...
 *
 * Electrical interface for E3F-DS10C4 (NPN, Normally Open):
 *   brown = +5..24 V
 *   blue  = GND
 *   black = OUT — pulls LOW when an object is within 3–10 cm
 *                 (open otherwise; an external pull-up keeps the line HIGH)
 *
 * Logical meaning emitted by this interface is ALREADY debounced and
 * already normalized (`present` means "vehicle in the detection zone"),
 * regardless of the underlying NPN/PNP or active-low/high wiring.
 */

#pragma once

#include <cstdint>
#include <functional>

enum class VehicleSensorError : int {
    Ok           =  0,
    InvalidArg   = -1,
    IoError      = -2,
    NotSupported = -3,
    NotOpen      = -4,
};

class IVehicleSensor {
public:
    virtual ~IVehicleSensor() = default;

    // ── Lifecycle ───────────────────────────────────────────────────────
    // Acquire the underlying line/file descriptor and start delivering
    // events. Returns Ok on success, a negative error code otherwise.
    [[nodiscard]] virtual VehicleSensorError open() = 0;
    virtual void close()                             = 0;
    [[nodiscard]] virtual bool isOpen() const        = 0;

    // ── State ───────────────────────────────────────────────────────────
    // True if the sensor currently reports a vehicle in the zone.
    // Thread-safe — may be read from any thread.
    [[nodiscard]] virtual bool isPresent() const = 0;

    // ── Event callback ──────────────────────────────────────────────────
    // Fired on every debounced edge. `present` reflects the new state.
    // Invoked on the driver's event thread; use a queued signal/slot
    // boundary to hop back to the GUI thread.
    using StateCallback = std::function<void(bool present)>;
    virtual void setStateCallback(StateCallback cb) = 0;

    // Perform a synchronous hardware read and update the cached present state.
    // Call from the GUI thread after a startup delay to catch vehicles that
    // were already blocking the sensor before the gate opened (no edge fires
    // in that case). Default: no-op for drivers that don't support live reads.
    virtual void pollLive() {}
};
