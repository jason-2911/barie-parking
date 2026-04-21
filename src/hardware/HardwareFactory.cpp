#include "hardware/HardwareFactory.h"
#include "hardware/drivers/BarrierControllerModbus.h"
#include "hardware/drivers/NV9ValidatorSSP.h"
#include "hardware/drivers/VehicleSensorGpiod.h"

std::unique_ptr<IBarrierController>
HardwareFactory::createBarrier(const Config& cfg) {
    // Only one driver type today; extend with a switch when new
    // transport types (CAN, TCP/IP, etc.) are added.
    return std::make_unique<BarrierControllerModbus>(
        cfg.serial.barrier_port, cfg.serial.barrier_baud);
}

std::unique_ptr<INV9Validator>
HardwareFactory::createValidator(const Config& cfg) {
    return std::make_unique<NV9ValidatorSSP>(cfg.serial.nv9_port);
}

std::unique_ptr<IVehicleSensor>
HardwareFactory::createVehicleSensor(const Config& cfg) {
    VehicleSensorGpiod::Options opts;
    opts.chip_path    = cfg.vehicle_sensor.chip_path.toStdString();
    opts.line_offset  = static_cast<uint32_t>(cfg.vehicle_sensor.line_offset);
    opts.active_low   = cfg.vehicle_sensor.active_low;
    opts.bias_pull_up = cfg.vehicle_sensor.bias_pull_up;
    opts.debounce_us  = static_cast<uint32_t>(cfg.vehicle_sensor.debounce_us);
    return std::make_unique<VehicleSensorGpiod>(std::move(opts));
}
