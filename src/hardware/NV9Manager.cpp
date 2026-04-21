#include "hardware/NV9Manager.h"
#include "hardware/HardwareFactory.h"

#include <QDebug>

NV9Manager::NV9Manager(const Config& cfg, QObject* parent)
    : QObject(parent), cfg_(cfg) {}

NV9Manager::~NV9Manager() { stop(); }

void NV9Manager::start() {
    if (!cfg_.device.has_cash_reader || running_) return;
    qInfo() << "[NV9] starting on" << cfg_.serial.nv9_port;

    driver_ = HardwareFactory::createValidator(cfg_);
    if (driver_->open() != NV9DriverError::OK) {
        emit errorOccurred("Cannot open NV9 port");
        driver_.reset();
        return;
    }

    driver_->setCreditCallback(
        [this](uint8_t ch, uint32_t amount, const QString&) {
            Q_UNUSED(ch)
            // Emitted from driver thread — Qt queues automatically
            // because the connected slot lives on the GUI thread.
            emit cashInserted(amount);
        });
    driver_->setFaultCallback(
        [this](const QString& desc) { emit errorOccurred(desc); });

    driver_->startPolling();
    running_ = true;
}

void NV9Manager::stop() {
    if (!running_) return;
    qInfo() << "[NV9] stopping";
    if (driver_) {
        driver_->stopPolling();
        driver_->close();
        driver_.reset();
    }
    running_ = false;
}

void NV9Manager::enableAccept() {
    if (driver_) driver_->enable();
}

void NV9Manager::disableAccept() {
    if (driver_) driver_->disable();
}

void NV9Manager::rejectNote() {
    if (driver_) driver_->rejectNote();
}

void NV9Manager::simulateCash(quint32 amount) {
    qInfo() << "[NV9][SIM] injecting credit" << amount;
    emit cashInserted(amount);
}
