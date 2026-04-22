/**
 * @file BarrierControllerStub.cpp
 * @brief See BarrierControllerStub.h
 */

#include "hardware/drivers/BarrierControllerStub.h"

#include <QDebug>

namespace {
constexpr const char* kTag = "[Barrier][MOCK]";
} // namespace

BarrierControllerStub::BarrierControllerStub() = default;

BarrierDriverError BarrierControllerStub::open() {
    std::lock_guard<std::mutex> lk(mutex_);
    open_ = true;
    qInfo() << kTag << "open()";
    return BarrierDriverError::OK;
}

void BarrierControllerStub::close() {
    std::lock_guard<std::mutex> lk(mutex_);
    open_ = false;
    qInfo() << kTag << "close()";
}

QVector<uint8_t>
BarrierControllerStub::scan(uint8_t start_addr, uint8_t end_addr) {
    std::lock_guard<std::mutex> lk(mutex_);
    qInfo().nospace() << kTag << " scan(" << start_addr << ".." << end_addr
                      << ") → " << slaves_.size() << " slave(s) (from addSlave)";
    return slaves_;
}

void BarrierControllerStub::addSlave(uint8_t addr) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!slaves_.contains(addr)) {
        slaves_.append(addr);
        Slot& s = slots_[addr];
        s.target   = BarrierDriverPosition::Down;
        s.ready_at = std::chrono::steady_clock::now();
    }
    qInfo() << kTag << "addSlave(" << addr << ")";
}

BarrierDriverError
BarrierControllerStub::setSlaveAddress(uint8_t cur, uint8_t neu) {
    std::lock_guard<std::mutex> lk(mutex_);
    qInfo().nospace() << kTag << " setSlaveAddress(" << cur << " → " << neu << ")";
    slaves_.removeAll(cur);
    if (!slaves_.contains(neu)) slaves_.append(neu);
    auto it = slots_.find(cur);
    if (it != slots_.end()) {
        slots_.insert(neu, it.value());
        slots_.erase(it);
    }
    return BarrierDriverError::OK;
}

void BarrierControllerStub::schedule(uint8_t slave,
                                      BarrierDriverPosition target) {
    Slot& s = slots_[slave];
    s.target   = target;
    s.ready_at = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(kTransitMs);
}

BarrierDriverPosition
BarrierControllerStub::currentPosition(uint8_t slave) const {
    auto it = slots_.find(slave);
    if (it == slots_.end()) return BarrierDriverPosition::Unknown;

    const auto now = std::chrono::steady_clock::now();
    if (now >= it->ready_at) return it->target;

    // Mid-transit reflects the direction of motion — useful for the UI to
    // render the cần actually moving instead of jumping straight to Up/Down.
    return (it->target == BarrierDriverPosition::Up)
            ? BarrierDriverPosition::MovingUp
            : BarrierDriverPosition::MovingDown;
}

BarrierDriverError BarrierControllerStub::raise(uint8_t slave) {
    std::lock_guard<std::mutex> lk(mutex_);
    qInfo() << kTag << "▲ RAISE  slave=" << slave;
    schedule(slave, BarrierDriverPosition::Up);
    return BarrierDriverError::OK;
}

BarrierDriverError BarrierControllerStub::lower(uint8_t slave) {
    std::lock_guard<std::mutex> lk(mutex_);
    qInfo() << kTag << "▼ LOWER  slave=" << slave;
    schedule(slave, BarrierDriverPosition::Down);
    return BarrierDriverError::OK;
}

BarrierDriverError BarrierControllerStub::pause(uint8_t slave) {
    std::lock_guard<std::mutex> lk(mutex_);
    qInfo() << kTag << "⏸ PAUSE  slave=" << slave;
    return BarrierDriverError::OK;
}

BarrierDriverError
BarrierControllerStub::readStatus(uint8_t slave, BarrierDriverStatus& out) {
    std::lock_guard<std::mutex> lk(mutex_);
    out = BarrierDriverStatus{};
    out.slave_addr  = slave;
    out.power_ok    = true;
    out.self_tested = true;
    out.position    = currentPosition(slave);
    out.is_running  = (out.position == BarrierDriverPosition::MovingUp ||
                       out.position == BarrierDriverPosition::MovingDown);
    out.direction_up = (out.position == BarrierDriverPosition::MovingUp ||
                        out.position == BarrierDriverPosition::Up);
    return BarrierDriverError::OK;
}

BarrierDriverError
BarrierControllerStub::readPosition(uint8_t slave, BarrierDriverPosition& out) {
    std::lock_guard<std::mutex> lk(mutex_);
    out = currentPosition(slave);
    return BarrierDriverError::OK;
}
