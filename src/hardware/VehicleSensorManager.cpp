#include "hardware/VehicleSensorManager.h"
#include "hardware/HardwareFactory.h"

#include <QDebug>

VehicleSensorManager::VehicleSensorManager(const Config& cfg, QObject* parent)
    : QObject(parent), cfg_(cfg) {}

VehicleSensorManager::~VehicleSensorManager() { shutdown(); }

void VehicleSensorManager::init() {
    if (driver_) return;

    if (!cfg_.vehicle_sensor.enabled) {
        qInfo() << "[VehicleSensor] disabled in config";
        return;
    }

    driver_ = HardwareFactory::createVehicleSensor(cfg_);
    if (!driver_) {
        qCritical() << "[VehicleSensor] factory returned null";
        return;
    }

    // Queued dispatch: the driver thread calls handleState(), which hops
    // onto this QObject's thread before emitting Qt signals.
    driver_->setStateCallback([this](bool present) {
        QMetaObject::invokeMethod(this, "handleState",
                                   Qt::QueuedConnection,
                                   Q_ARG(bool, present));
    });

    auto err = driver_->open();
    if (err != VehicleSensorError::Ok) {
        qCritical() << "[VehicleSensor] open failed, code="
                    << static_cast<int>(err);
        driver_.reset();
        return;
    }

    qInfo() << "[VehicleSensor] initial present="
            << driver_->isPresent();
}

void VehicleSensorManager::shutdown() {
    if (!driver_) return;
    driver_->setStateCallback(nullptr);
    driver_->close();
    driver_.reset();
}

bool VehicleSensorManager::isPresent() const {
    return driver_ && driver_->isPresent();
}

bool VehicleSensorManager::isOpen() const {
    return driver_ && driver_->isOpen();
}

void VehicleSensorManager::handleState(bool present) {
    if (present) emit vehiclePresent();
    else         emit vehicleCleared();
}
