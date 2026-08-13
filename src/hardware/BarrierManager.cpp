#include "hardware/BarrierManager.h"
#include "hardware/HardwareFactory.h"
#include "hardware/VehicleSensorManager.h"

#include <QTimer>
#include <QDebug>

BarrierManager::BarrierManager(const Config& cfg,
                                VehicleSensorManager* sensor,
                                QObject* parent)
    : QObject(parent), cfg_(cfg), sensor_(sensor)
{
    for (const auto& b : cfg_.barriers) {
        positions_[b.id] = "down";
        online_[b.id]    = false;
        fsm_[b.id]       = {};
    }
}

BarrierManager::~BarrierManager() {
    for (auto& slot : fsm_) cancelTimers(slot);
    if (driver_) driver_->close();
}

// ── Helpers ──────────────────────────────────────────────────────────────────

const char* BarrierManager::stateName(State s) {
    switch (s) {
        case State::Idle:            return "Idle";
        case State::WaitingVehicle:  return "WaitingVehicle";
        case State::Passing:         return "Passing";
        case State::ClearDelay:      return "ClearDelay";
        case State::Lowering:        return "Lowering";
    }
    return "?";
}

QString BarrierManager::positionToString(BarrierDriverPosition p) {
    switch (p) {
        case BarrierDriverPosition::Up:          return "up";
        case BarrierDriverPosition::Down:        return "down";
        case BarrierDriverPosition::MovingUp:    return "moving_up";
        case BarrierDriverPosition::MovingDown:  return "moving_down";
        default:                                 return "unknown";
    }
}

uint8_t BarrierManager::modbusAddrFor(const QString& barrier_id) const {
    if (const auto* b = cfg_.findBarrier(barrier_id))
        return static_cast<uint8_t>(b->modbus_addr);
    return 0;
}

QString BarrierManager::barrierIdFor(uint8_t modbus_addr) const {
    for (const auto& b : cfg_.barriers)
        if (b.modbus_addr == modbus_addr) return b.id;
    return {};
}

void BarrierManager::cancelTimers(FsmSlot& slot) {
    if (slot.clear_timer  && slot.clear_timer->isActive())  slot.clear_timer->stop();
    if (slot.safety_timer && slot.safety_timer->isActive()) slot.safety_timer->stop();
}

QString BarrierManager::resolveSensorBarrier(const QString& barrier_id) const {
    // Direct routing: a sensor bound to a lane names its barrier explicitly.
    if (!barrier_id.isEmpty() && fsm_.contains(barrier_id))
        return barrier_id;
    // Legacy single-lane: the sensor had no binding — fall back to the
    // heuristic that picks the barrier furthest into the flow.
    return activeBarrierForSensor();
}

int BarrierManager::clearDelayForBarrier(const QString& barrier_id) const {
    if (const auto* s = cfg_.sensorForBarrier(barrier_id))
        return s->clear_delay_ms;
    return 1500;   // default when no sensor is bound to this lane
}

QString BarrierManager::activeBarrierForSensor() const {
    // Single-lane assumption: exactly one barrier waits for the sensor at a
    // time. If multiple are waiting we pick the one furthest into the flow,
    // Passing > WaitingVehicle, which matches the likely real-world case
    // (the barrier that just raised is the one the vehicle is approaching).
    QString best;
    State   rank = State::Idle;
    for (auto it = fsm_.begin(); it != fsm_.end(); ++it) {
        const State s = it->state;
        if (s == State::Passing ||
            s == State::WaitingVehicle ||
            s == State::ClearDelay) {
            if (best.isEmpty() || s > rank) { best = it.key(); rank = s; }
        }
    }
    return best;
}

// ── Init / polling ───────────────────────────────────────────────────────────

bool BarrierManager::init() {
    qInfo() << "[Barrier] initializing on" << cfg_.serial.barrier_port;

    driver_ = HardwareFactory::createBarrier(cfg_);
    auto err = driver_->open();
    if (err != BarrierDriverError::OK) {
        qCritical() << "[Barrier] driver open failed:"
                    << static_cast<int>(err);
        return false;
    }

    for (const auto& b : cfg_.barriers)
        driver_->addSlave(static_cast<uint8_t>(b.modbus_addr));

    driver_->setPositionCallback(
        [this](uint8_t addr, BarrierDriverPosition pos) {
            QString id = barrierIdFor(addr);
            if (id.isEmpty()) return;
            positions_[id] = positionToString(pos);
            emit statusChanged(id, positions_[id]);
        });

    for (const auto& b : cfg_.barriers) {
        online_[b.id]    = true;
        positions_[b.id] = "down";
        emit statusChanged(b.id, "down");
        qInfo() << "[Barrier] registered" << b.id
                << "addr=" << b.modbus_addr;
    }

    if (sensor_) {
        connect(sensor_, &VehicleSensorManager::vehiclePresent,
                this,    &BarrierManager::onVehiclePresent);
        connect(sensor_, &VehicleSensorManager::vehicleCleared,
                this,    &BarrierManager::onVehicleCleared);
    }

    poll_timer_ = new QTimer(this);
    connect(poll_timer_, &QTimer::timeout, this, &BarrierManager::pollStatus);
    poll_timer_->start(2000);
    return true;
}

void BarrierManager::closeAll() {
    for (const auto& b : cfg_.barriers) {
        cancelTimers(fsm_[b.id]);
        rawLower(b.id);
        enterIdle(b.id);
    }
}

bool BarrierManager::isOnline(const QString& id) const {
    return online_.value(id, false);
}

QString BarrierManager::position(const QString& id) const {
    return positions_.value(id, "unknown");
}

void BarrierManager::pollStatus() {
    if (!driver_) return;
    for (const auto& b : cfg_.barriers) {
        BarrierDriverPosition pos;
        auto err = driver_->readPosition(
            static_cast<uint8_t>(b.modbus_addr), pos);
        if (err == BarrierDriverError::OK) {
            QString p = positionToString(pos);
            if (positions_.value(b.id) != p) {
                positions_[b.id] = p;
                emit statusChanged(b.id, p);
            }
        }
    }
}

// ── High-level requests ──────────────────────────────────────────────────────

void BarrierManager::requestOpen(const QString& barrier_id, int auto_close_sec) {
    if (!fsm_.contains(barrier_id)) {
        qWarning() << "[Barrier] requestOpen: unknown barrier" << barrier_id;
        return;
    }
    FsmSlot& slot       = fsm_[barrier_id];
    slot.auto_close_sec = auto_close_sec > 0 ? auto_close_sec : slot.auto_close_sec;

    const State prev = slot.state;
    qInfo().nospace() << "[Barrier] OPEN requested on " << barrier_id
                      << " (state=" << stateName(prev)
                      << ", auto_close=" << slot.auto_close_sec << "s)";

    switch (prev) {
        case State::Idle:
        case State::Lowering: {
            // Either cold start OR reopen-during-close. Both paths raise the
            // gate and re-arm safety timer — the mid-close case is the
            // critical one that keeps a vehicle from being hit.
            cancelTimers(slot);
            rawRaise(barrier_id);
            enterWaiting(barrier_id);
            break;
        }
        case State::ClearDelay: {
            // Was about to close; caller wants it to stay up.
            cancelTimers(slot);
            enterWaiting(barrier_id);
            break;
        }
        case State::WaitingVehicle:
        case State::Passing: {
            // Already up; just refresh the safety timer.
            armSafety(barrier_id, slot);
            break;
        }
    }
}

void BarrierManager::requestClose(const QString& barrier_id) {
    if (!fsm_.contains(barrier_id)) return;
    FsmSlot& slot = fsm_[barrier_id];
    qInfo().nospace() << "[Barrier] CLOSE requested on " << barrier_id
                      << " (state=" << stateName(slot.state) << ")";
    cancelTimers(slot);
    rawLower(barrier_id);
    enterLowering(barrier_id);
}

// ── State transitions ────────────────────────────────────────────────────────

void BarrierManager::enterWaiting(const QString& barrier_id) {
    FsmSlot& slot = fsm_[barrier_id];
    slot.state = State::WaitingVehicle;
    qInfo() << "[Barrier]" << barrier_id << "→ WaitingVehicle";
    armSafety(barrier_id, slot);

    // Deferred pre-presence check: do NOT call isPresent() immediately here.
    // At startup the barrier arm is DOWN and often physically blocks the
    // sensor beam — isPresent() would return true even with no vehicle, and
    // the gate would jump straight to Passing on every open.
    // Instead, wait 2 s for the arm to clear, then do a live hardware re-read
    // (pollLive). If a vehicle was genuinely pre-present the read will return
    // true; normal edge events still fire at any time and are handled by
    // onVehiclePresent() regardless of this timer.
    if (sensor_) {
        const QString id = barrier_id;
        QTimer::singleShot(2000, this, [this, id]() {
            if (!fsm_.contains(id) || fsm_[id].state != State::WaitingVehicle) return;
            sensor_->pollLive(id);
            if (sensor_->isPresent(id)) {
                qInfo() << "[Barrier]" << id
                        << "deferred poll: vehicle pre-present, entering Passing";
                enterPassing(id);
            }
        });
    }

    if (cfg_.self_test.enabled && cfg_.self_test.auto_vehicle_sensor) {
        const QString id = barrier_id;
        QTimer::singleShot(cfg_.self_test.vehicle_present_delay_ms, this, [this, id]() {
            if (!fsm_.contains(id)) return;
            const State s = fsm_[id].state;
            if (s == State::WaitingVehicle || s == State::ClearDelay)
                onVehiclePresent(id);
        });
        QTimer::singleShot(cfg_.self_test.vehicle_clear_delay_ms, this, [this, id]() {
            if (!fsm_.contains(id)) return;
            if (fsm_[id].state == State::Passing)
                onVehicleCleared(id);
        });
    }
}

void BarrierManager::enterPassing(const QString& barrier_id) {
    FsmSlot& slot = fsm_[barrier_id];
    slot.state = State::Passing;
    // Restart (not stop) the safety timer. The vehicle must now clear within
    // auto_close_sec. If the sensor never clears — stuck vehicle, sensor
    // fault, or startup false-positive — the gate force-closes anyway.
    if (slot.safety_timer && slot.auto_close_sec > 0)
        slot.safety_timer->start(slot.auto_close_sec * 1000);
    qInfo() << "[Barrier]" << barrier_id << "→ Passing";
    emit vehiclePassing(barrier_id);
}

void BarrierManager::enterClearDelay(const QString& barrier_id) {
    FsmSlot& slot = fsm_[barrier_id];
    slot.state = State::ClearDelay;
    const int clear_delay_ms = clearDelayForBarrier(barrier_id);
    qInfo() << "[Barrier]" << barrier_id << "→ ClearDelay"
            << clear_delay_ms << "ms";
    emit vehiclePassed(barrier_id);

    if (!slot.clear_timer) {
        slot.clear_timer = new QTimer(this);
        slot.clear_timer->setSingleShot(true);
        const QString id = barrier_id;
        connect(slot.clear_timer, &QTimer::timeout, this, [this, id]() {
            if (!fsm_.contains(id) || fsm_[id].state != State::ClearDelay) return;
            rawLower(id);
            enterLowering(id);
        });
    }
    slot.clear_timer->start(clear_delay_ms);
}

void BarrierManager::enterLowering(const QString& barrier_id) {
    FsmSlot& slot = fsm_[barrier_id];
    cancelTimers(slot);   // stop safety/clear timers before they fire during descent
    slot.state = State::Lowering;
    qInfo() << "[Barrier]" << barrier_id << "→ Lowering";
    emit autoClosed(barrier_id);

    // Hand control back to Idle once the gate should be physically down.
    // The polled driver status will correct this if the gate disagrees.
    QTimer::singleShot(3000, this, [this, barrier_id]() {
        if (fsm_.contains(barrier_id) && fsm_[barrier_id].state == State::Lowering) {
            enterIdle(barrier_id);
        }
    });
}

void BarrierManager::enterIdle(const QString& barrier_id) {
    FsmSlot& slot = fsm_[barrier_id];
    cancelTimers(slot);
    slot.state = State::Idle;
    qInfo() << "[Barrier]" << barrier_id << "→ Idle";
}

void BarrierManager::armSafety(const QString& barrier_id, FsmSlot& slot) {
    if (slot.auto_close_sec <= 0) return;
    if (!slot.safety_timer) {
        slot.safety_timer = new QTimer(this);
        slot.safety_timer->setSingleShot(true);
        const QString id = barrier_id;
        connect(slot.safety_timer, &QTimer::timeout, this, [this, id]() {
            if (!fsm_.contains(id)) return;
            const auto s = fsm_[id].state;
            if (s != State::WaitingVehicle && s != State::Passing) return;
            qWarning() << "[Barrier]" << id
                       << "safety timeout — forcing close (state=" << stateName(s) << ")";
            rawLower(id);
            enterLowering(id);
        });
    }
    slot.safety_timer->start(slot.auto_close_sec * 1000);
}

// ── Sensor callbacks ─────────────────────────────────────────────────────────

void BarrierManager::onVehiclePresent(const QString& barrier_id) {
    const QString id = resolveSensorBarrier(barrier_id);
    if (id.isEmpty()) {
        qDebug() << "[Barrier] sensor: present — no active barrier, ignored";
        return;
    }
    FsmSlot& slot = fsm_[id];
    if (slot.state == State::WaitingVehicle || slot.state == State::ClearDelay) {
        cancelTimers(slot);
        enterPassing(id);
    }
}

void BarrierManager::onVehicleCleared(const QString& barrier_id) {
    const QString id = resolveSensorBarrier(barrier_id);
    if (id.isEmpty()) return;
    FsmSlot& slot = fsm_[id];
    if (slot.state == State::Passing) {
        enterClearDelay(id);
    }
}

// ── Mechanical I/O ───────────────────────────────────────────────────────────

void BarrierManager::rawRaise(const QString& barrier_id) {
    if (!driver_) return;
    const uint8_t addr = modbusAddrFor(barrier_id);

    // One immediate retry on transient Modbus errors (CRC, timeout).
    // Do NOT update position or state if the command ultimately fails —
    // a gate that didn't actually move must not be treated as open.
    BarrierDriverError err = driver_->raise(addr);
    if (err != BarrierDriverError::OK) {
        qWarning() << "[Barrier] raise" << barrier_id
                   << "failed (err=" << static_cast<int>(err) << "), retrying once";
        err = driver_->raise(addr);
    }
    if (err != BarrierDriverError::OK) {
        qCritical() << "[Barrier] raise" << barrier_id
                    << "failed after retry (err=" << static_cast<int>(err) << ")";
        emit faultDetected(barrier_id,
                           QString("raise failed err=%1").arg(static_cast<int>(err)));
        return;
    }
    positions_[barrier_id] = "up";
    emit statusChanged(barrier_id, "up");
}

void BarrierManager::rawLower(const QString& barrier_id) {
    if (!driver_) return;
    const uint8_t addr = modbusAddrFor(barrier_id);

    BarrierDriverError err = driver_->lower(addr);
    if (err != BarrierDriverError::OK) {
        qWarning() << "[Barrier] lower" << barrier_id
                   << "failed (err=" << static_cast<int>(err) << "), retrying once";
        err = driver_->lower(addr);
    }
    if (err != BarrierDriverError::OK) {
        qCritical() << "[Barrier] lower" << barrier_id
                    << "failed after retry (err=" << static_cast<int>(err) << ")";
        emit faultDetected(barrier_id,
                           QString("lower failed err=%1").arg(static_cast<int>(err)));
        return;
    }
    positions_[barrier_id] = "down";
    emit statusChanged(barrier_id, "down");
}
