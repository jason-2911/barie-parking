/**
 * IBarrierController.h
 * Abstract interface for barrier gate controllers.
 *
 * Concrete implementations:
 *   - BarrierControllerModbus (Modbus RTU over RS485)
 *   - (future) BarrierControllerCAN, BarrierControllerTCP, ...
 *
 * BarrierManager depends only on this interface, so new driver types
 * can be plugged in via HardwareFactory without touching orchestration code.
 */

#pragma once

#include <QString>
#include <QVector>
#include <functional>
#include <cstdint>

enum class BarrierDriverError {
    OK              =  0,
    INVALID_ARG     = -1,
    IO_ERROR        = -2,
    TIMEOUT         = -3,
    CRC_MISMATCH    = -4,
    SLAVE_NOT_FOUND = -5,
    DEVICE_FAULT    = -6,
    NOT_OPEN        = -7,
};

enum class BarrierDriverPosition {
    Unknown,
    Up,
    Down,
    MovingUp,
    MovingDown,
};

struct BarrierDriverStatus {
    uint8_t  slave_addr = 0;
    bool     is_running = false;
    bool     direction_up = false;
    bool     power_ok = true;
    bool     self_tested = false;
    BarrierDriverPosition position = BarrierDriverPosition::Unknown;
    uint16_t fault_code = 0;
    QString  error_string;
};

class IBarrierController {
public:
    virtual ~IBarrierController() = default;

    // ── Lifecycle ───────────────────────────────────────────────────────
    virtual BarrierDriverError open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // ── Discovery ───────────────────────────────────────────────────────
    virtual QVector<uint8_t> scan(uint8_t start_addr = 1,
                                   uint8_t end_addr = 32) = 0;
    virtual void addSlave(uint8_t addr) = 0;

    // Rewrite a barrier's own Modbus slave address via FC06. The caller must
    // ensure only one barrier is physically connected during the operation —
    // otherwise multiple units with the same current_addr will corrupt each
    // other's registers. Intended for provisioning only.
    virtual BarrierDriverError setSlaveAddress(uint8_t current_addr,
                                                uint8_t new_addr) = 0;

    // ── Control ─────────────────────────────────────────────────────────
    virtual BarrierDriverError raise(uint8_t slave_addr) = 0;
    virtual BarrierDriverError lower(uint8_t slave_addr) = 0;
    virtual BarrierDriverError pause(uint8_t slave_addr) = 0;

    // ── Status ──────────────────────────────────────────────────────────
    virtual BarrierDriverError readStatus(uint8_t slave_addr,
                                           BarrierDriverStatus& out) = 0;
    virtual BarrierDriverError readPosition(uint8_t slave_addr,
                                             BarrierDriverPosition& out) = 0;

    // ── Events ──────────────────────────────────────────────────────────
    using PositionCallback = std::function<void(uint8_t slave_addr,
                                                 BarrierDriverPosition pos)>;
    virtual void setPositionCallback(PositionCallback cb) = 0;
};
