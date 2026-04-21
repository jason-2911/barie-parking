/**
 * NV9ValidatorSSP.h
 * ITL NV9 USB Bill Validator — eSSP (Smiley Secure Protocol) driver,
 * implements INV9Validator.
 *
 * On macOS the serial I/O and polling thread are stubbed; the public
 * interface just logs so the UI demo can be exercised without hardware.
 */

#pragma once

#include "hardware/INV9Validator.h"

#include <QString>
#include <QVector>
#include <atomic>
#include <mutex>
#include <thread>
#include <cstdint>

class NV9ValidatorSSP : public INV9Validator {
public:
    NV9ValidatorSSP(const QString& port, uint8_t slave_addr = 0x00);
    ~NV9ValidatorSSP() override;

    NV9ValidatorSSP(const NV9ValidatorSSP&) = delete;
    NV9ValidatorSSP& operator=(const NV9ValidatorSSP&) = delete;

    // ── INV9Validator ───────────────────────────────────────────────────
    NV9DriverError open() override;
    void close() override;
    bool isOpen() const override;

    NV9DriverError enable() override;
    NV9DriverError disable() override;
    NV9DriverError rejectNote() override;
    NV9DriverError holdNote() override;
    NV9DriverError reset() override;
    NV9DriverState state() const override { return state_.load(); }

    void startPolling() override;
    void stopPolling() override;
    bool isPolling() const override { return polling_.load(); }

    void setCreditCallback(CreditCallback cb) override { credit_cb_ = cb; }
    void setFaultCallback(FaultCallback cb)   override { fault_cb_  = cb; }

private:
    // Polling loop
    void pollThreadFunc();

    // Serial I/O (real on Linux, stubbed on __APPLE__)
    NV9DriverError serialWriteRaw(const uint8_t* data, size_t len);
    NV9DriverError serialReadRaw(uint8_t* buf, size_t max_len,
                                  int timeout_ms, size_t& read_out);
    void serialFlush();

    // CRC-16 CCITT (polynomial 0x8005)
    static uint16_t crc16Ccitt(const uint8_t* data, size_t len);

    QString                 port_;
    uint8_t                 slave_addr_;
    int                     fd_ = -1;
    uint8_t                 seq_bit_ = 0;
    std::mutex              io_mutex_;

    std::atomic<NV9DriverState> state_{NV9DriverState::Idle};
    std::atomic<bool>       polling_{false};
    std::thread             poll_thread_;

    CreditCallback          credit_cb_;
    FaultCallback           fault_cb_;
};
