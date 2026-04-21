/**
 * HardwareFactory.h
 * Single point where the concrete hardware driver implementation is chosen.
 *
 * Swap the factory body to plug in a different barrier / validator
 * vendor without touching the managers or the application layer.
 */

#pragma once

#include <memory>
#include "app/Config.h"
#include "hardware/IBarrierController.h"
#include "hardware/INV9Validator.h"
#include "hardware/IVehicleSensor.h"

class HardwareFactory {
public:
    // Builds the barrier driver declared for `cfg.serial.barrier_port`.
    // On macOS the underlying driver is auto-stubbed by __APPLE__.
    static std::unique_ptr<IBarrierController>
    createBarrier(const Config& cfg);

    // Builds the cash validator driver declared for `cfg.serial.nv9_port`.
    static std::unique_ptr<INV9Validator>
    createValidator(const Config& cfg);

    // Builds the vehicle presence sensor (E3F-DS10C4 today).
    // On macOS the underlying driver is auto-stubbed by __APPLE__.
    static std::unique_ptr<IVehicleSensor>
    createVehicleSensor(const Config& cfg);
};
