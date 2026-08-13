#include "hardware/VehicleSensorManager.h"
#include "hardware/HardwareFactory.h"

#include <utility>

#include <QDebug>

VehicleSensorManager::VehicleSensorManager(const Config& cfg, QObject* parent)
    : QObject(parent), cfg_(cfg) {}

VehicleSensorManager::~VehicleSensorManager() { shutdown(); }

void VehicleSensorManager::init() {
    if (!lanes_.empty()) return;

    if (cfg_.vehicle_sensors.isEmpty()) {
        qInfo() << "[VehicleSensor] no sensors configured";
        return;
    }

    for (const auto& vs : cfg_.vehicle_sensors) {
        if (!vs.enabled) {
            qInfo() << "[VehicleSensor] disabled in config for barrier"
                    << vs.barrier_id;
            continue;
        }

        auto driver = HardwareFactory::createVehicleSensor(vs);
        if (!driver) {
            qCritical() << "[VehicleSensor] factory returned null for barrier"
                        << vs.barrier_id;
            continue;
        }

        // Capture the lane's barrier id so the queued dispatch knows which
        // lane fired. The driver thread calls this lambda, which hops onto
        // this QObject's thread before emitting the Qt signal.
        const QString bid = vs.barrier_id;
        driver->setStateCallback([this, bid](bool present) {
            QMetaObject::invokeMethod(this, "handleState",
                                       Qt::QueuedConnection,
                                       Q_ARG(QString, bid),
                                       Q_ARG(bool, present));
        });

        auto err = driver->open();
        if (err != VehicleSensorError::Ok) {
            qCritical() << "[VehicleSensor] open failed for barrier" << bid
                        << "code=" << static_cast<int>(err);
            continue;
        }

        qInfo() << "[VehicleSensor] barrier" << bid
                << "line=" << vs.line_offset
                << "initial present=" << driver->isPresent();
        lanes_.push_back(Lane{bid, std::move(driver)});
    }
}

void VehicleSensorManager::shutdown() {
    for (auto& lane : lanes_) {
        if (!lane.driver) continue;
        lane.driver->setStateCallback(nullptr);
        lane.driver->close();
    }
    lanes_.clear();
}

bool VehicleSensorManager::isPresent(const QString& barrier_id) const {
    for (const auto& lane : lanes_)
        if (lane.barrier_id == barrier_id)
            return lane.driver && lane.driver->isPresent();
    return false;
}

void VehicleSensorManager::pollLive(const QString& barrier_id) {
    for (auto& lane : lanes_)
        if (lane.barrier_id == barrier_id && lane.driver)
            lane.driver->pollLive();
}

bool VehicleSensorManager::isOpen() const {
    for (const auto& lane : lanes_)
        if (lane.driver && lane.driver->isOpen()) return true;
    return false;
}

void VehicleSensorManager::handleState(const QString& barrier_id, bool present) {
    if (present) emit vehiclePresent(barrier_id);
    else         emit vehicleCleared(barrier_id);
}
