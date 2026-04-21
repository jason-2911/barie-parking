/**
 * INV9Validator.h
 * Abstract interface for bill validator / cash acceptor devices.
 *
 * Concrete implementations:
 *   - NV9ValidatorSSP (ITL NV9 eSSP over serial)
 *   - (future) other acceptors
 */

#pragma once

#include <QString>
#include <functional>
#include <cstdint>

enum class NV9DriverError {
    OK              =  0,
    INVALID_ARG     = -1,
    IO_ERROR        = -2,
    TIMEOUT         = -3,
    CRC_MISMATCH    = -4,
    RSP_FAIL        = -5,
    NOT_OPEN        = -6,
    PROTOCOL_ERROR  = -7,
};

enum class NV9DriverState {
    Idle, Disabled, Enabled, Reading,
    Stacking, Stacked, Rejecting, Jammed, Fault,
};

class INV9Validator {
public:
    virtual ~INV9Validator() = default;

    // ── Lifecycle ───────────────────────────────────────────────────────
    virtual NV9DriverError open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // ── Control ─────────────────────────────────────────────────────────
    virtual NV9DriverError enable() = 0;
    virtual NV9DriverError disable() = 0;
    virtual NV9DriverError rejectNote() = 0;
    virtual NV9DriverError holdNote() = 0;
    virtual NV9DriverError reset() = 0;
    virtual NV9DriverState state() const = 0;

    // ── Background poll loop ───────────────────────────────────────────
    virtual void startPolling() = 0;
    virtual void stopPolling() = 0;
    virtual bool isPolling() const = 0;

    // ── Events ──────────────────────────────────────────────────────────
    using CreditCallback = std::function<void(uint8_t channel,
                                               uint32_t amount,
                                               const QString& currency)>;
    using FaultCallback  = std::function<void(const QString& desc)>;

    virtual void setCreditCallback(CreditCallback cb) = 0;
    virtual void setFaultCallback(FaultCallback cb) = 0;
};
