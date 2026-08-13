#include "hardware/HardwareFactory.h"
#include "hardware/drivers/BarrierControllerModbus.h"
#include "hardware/drivers/BarrierControllerStub.h"
#include "hardware/drivers/NV9ValidatorSSP.h"
#include "hardware/drivers/VehicleSensorGpiod.h"

#include <QDebug>

std::unique_ptr<IBarrierController>
HardwareFactory::createBarrier(const Config& cfg) {
    // Bench-test profile: cảm biến + UI thật, nhưng không có barrier vật lý.
    if (cfg.serial.barrier_mock) {
        qInfo() << "[HW] serial.barrier_mock=true → using BarrierControllerStub";
        return std::make_unique<BarrierControllerStub>();
    }
    return std::make_unique<BarrierControllerModbus>(
        cfg.serial.barrier_port, cfg.serial.barrier_baud);
}

std::unique_ptr<INV9Validator>
HardwareFactory::createValidator(const Config& cfg) {
    return std::make_unique<NV9ValidatorSSP>(cfg.serial.nv9_port);
}

std::unique_ptr<IVehicleSensor>
HardwareFactory::createVehicleSensor(const VehicleSensorConfig& vs) {
    VehicleSensorGpiod::Options opts;
    opts.chip_path    = vs.chip_path.toStdString();
    opts.line_offset  = static_cast<uint32_t>(vs.line_offset);
    opts.active_low   = vs.active_low;
    opts.bias_pull_up = vs.bias_pull_up;
    opts.debounce_us  = static_cast<uint32_t>(vs.debounce_us);
    // Tag the consumer so `gpioinfo` shows which lane owns each line when a
    // device drives two sensors on the same chip.
    if (!vs.barrier_id.isEmpty())
        opts.consumer_label = "parking-vehicle-sensor-" + vs.barrier_id.toStdString();
    return std::make_unique<VehicleSensorGpiod>(std::move(opts));
}
