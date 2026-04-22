/**
 * @file BarrierControllerStub.h
 * @brief Logging-only barrier driver for bench-test deployments.
 *
 * Used when the device has the real cảm biến + UI but no physical barrier
 * yet. Every command is logged at INFO level and the internal position
 * transitions to Up shortly after raise() (and Down shortly after lower()),
 * so BarrierManager's poll loop sees the same state machine it would with
 * real hardware. scan() returns the addresses set with addSlave() so
 * upstream code believes every configured barrier is online.
 *
 * Selected via `serial.barrier_mock = true` in config.json.
 */

#pragma once

#include "hardware/IBarrierController.h"

#include <QMap>
#include <QVector>
#include <chrono>
#include <mutex>

class BarrierControllerStub final : public IBarrierController {
public:
    BarrierControllerStub();
    ~BarrierControllerStub() override = default;

    BarrierControllerStub(const BarrierControllerStub&)            = delete;
    BarrierControllerStub& operator=(const BarrierControllerStub&) = delete;

    BarrierDriverError open() override;
    void               close() override;
    bool               isOpen() const override { return open_; }

    QVector<uint8_t> scan(uint8_t start_addr = 1,
                           uint8_t end_addr = 32) override;
    void addSlave(uint8_t addr) override;

    BarrierDriverError setSlaveAddress(uint8_t current_addr,
                                        uint8_t new_addr) override;

    BarrierDriverError raise(uint8_t slave) override;
    BarrierDriverError lower(uint8_t slave) override;
    BarrierDriverError pause(uint8_t slave) override;

    BarrierDriverError readStatus(uint8_t slave,
                                   BarrierDriverStatus& out) override;
    BarrierDriverError readPosition(uint8_t slave,
                                     BarrierDriverPosition& out) override;

    void setPositionCallback(PositionCallback cb) override { event_cb_ = std::move(cb); }

private:
    // Per-slave fake state — moves Up/Down `kTransitMs` after a command.
    struct Slot {
        BarrierDriverPosition target = BarrierDriverPosition::Down;
        std::chrono::steady_clock::time_point ready_at;
    };

    BarrierDriverPosition currentPosition(uint8_t slave) const;
    void   schedule(uint8_t slave, BarrierDriverPosition target);

    static constexpr int kTransitMs = 600;   // simulated raise/lower duration

    bool                       open_ = false;
    mutable std::mutex         mutex_;
    QVector<uint8_t>           slaves_;
    mutable QMap<uint8_t, Slot> slots_;
    PositionCallback           event_cb_;
};
