/**
 * @file NV9ValidatorSSP.h
 * @brief ITL NV9 USB bill validator — eSSP (Smiley Secure Protocol) driver.
 *
 * Implements INV9Validator over a POSIX serial port (ttyACM / ttyUSB).
 * A background poll thread runs the state machine; all serial I/O is
 * serialised through io_mutex_. Thread-safe for concurrent enable(),
 * disable(), rejectNote() calls from the application thread.
 *
 * macOS: fully stubbed so the UI can be exercised without hardware.
 *
 * @author  Jason
 * @date    2026-05-04
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "hardware/INV9Validator.h"

#include <QString>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

class NV9ValidatorSSP : public INV9Validator {
public:
    explicit NV9ValidatorSSP(const QString& port, uint8_t slave_addr = 0x00);
    ~NV9ValidatorSSP() override;

    NV9ValidatorSSP(const NV9ValidatorSSP&)            = delete;
    NV9ValidatorSSP& operator=(const NV9ValidatorSSP&) = delete;

    // ── INV9Validator ────────────────────────────────────────────────────
    [[nodiscard]] NV9DriverError open()    override;
    void                         close()   override;
    [[nodiscard]] bool           isOpen()  const override;

    [[nodiscard]] NV9DriverError enable()     override;
    [[nodiscard]] NV9DriverError disable()    override;
    [[nodiscard]] NV9DriverError rejectNote() override;
    [[nodiscard]] NV9DriverError holdNote()   override;
    [[nodiscard]] NV9DriverError reset()      override;

    NV9DriverState state()     const override { return state_.load(); }
    bool           isPolling() const override { return polling_.load(); }

    void startPolling() override;
    void stopPolling()  override;

    void setCreditCallback(CreditCallback cb) override { credit_cb_ = std::move(cb); }
    void setFaultCallback(FaultCallback cb)   override { fault_cb_  = std::move(cb); }

private:
    // ── Protocol geometry ─────────────────────────────────────────────────
    static constexpr int kMaxChannels = 16;
    static constexpr int kMaxFrameLen = 256;

    // ── Serial port helpers ───────────────────────────────────────────────
    [[nodiscard]] NV9DriverError openSerialPort();
    void                         closeSerialPort();

    // ── Raw I/O ───────────────────────────────────────────────────────────
    [[nodiscard]] NV9DriverError serialWriteRaw(const uint8_t* data, size_t len);
    [[nodiscard]] NV9DriverError serialReadRaw(uint8_t* buf, size_t max_len,
                                               int timeout_ms, size_t& read_out);
    void serialFlush();

    // ── CRC ───────────────────────────────────────────────────────────────
    [[nodiscard]] static uint16_t crc16Ccitt(const uint8_t* data,
                                              size_t len) noexcept;

    // ── SSP framing ───────────────────────────────────────────────────────

    /**
     * @brief Build a byte-stuffed SSP frame into out[].
     * @return Frame length, or negative errno on error.
     * @pre Caller holds io_mutex_
     */
    [[nodiscard]] int buildFrame(const uint8_t* payload, int payload_len,
                                 uint8_t* out, int out_size) const noexcept;

    /**
     * @brief Send cmd, wait, read back response.
     * @pre Caller holds io_mutex_
     * @post seq_bit_ toggled on successful write.
     */
    [[nodiscard]] NV9DriverError sendCmdLocked(const char* name,
                                               const uint8_t* cmd, int cmd_len,
                                               uint8_t* resp, int& resp_len);

    /**
     * @brief Acquire io_mutex_ then call sendCmdLocked().
     * @thread_safety Safe to call from any thread.
     */
    [[nodiscard]] NV9DriverError sendCmd(const char* name,
                                         const uint8_t* cmd, int cmd_len,
                                         uint8_t* resp, int& resp_len);

    [[nodiscard]] static bool isRespOk(const uint8_t* resp, int len) noexcept;

    // ── SSP protocol handlers ─────────────────────────────────────────────

    /**
     * @brief SYNC → SETUP_REQUEST → CH_INHIBITS → ENABLE handshake.
     * @pre fd_ >= 0, io_mutex_ NOT held by caller.
     */
    [[nodiscard]] NV9DriverError initSequence();

    /**
     * @brief Parse SETUP_REQUEST response; populate channel_value_[].
     * @pre io_mutex_ held
     */
    void parseSetupRequest(const uint8_t* resp, int len);

    /**
     * @brief Dispatch SSP POLL events; call credit_cb_ / fault_cb_.
     * @return  0  normal
     *          1  EVT_SLAVE_RESET — caller should re-run initSequence()
     *         -1  frame error
     * @pre io_mutex_ NOT held (callbacks may take arbitrary time)
     */
    int parsePoll(const uint8_t* resp, int len);

    // ── Poll thread ───────────────────────────────────────────────────────
    void pollThreadFunc();

    // ── Members ───────────────────────────────────────────────────────────
    QString                     port_;
    uint8_t                     slave_addr_;
    // fd_ is read from both the poll thread and the main thread (isOpen,
    // open). Using atomic<int> makes every load/store individually safe
    // and avoids the data race without expanding the mutex scope into callers
    // that only need a quick "is it open?" check.
    std::atomic<int>            fd_{-1};
    uint8_t                     seq_bit_          = 0;
    mutable std::mutex          io_mutex_;           // guards all serial I/O + seq_bit_

    std::atomic<NV9DriverState> state_{NV9DriverState::Idle};
    std::atomic<bool>           polling_{false};
    std::atomic<bool>           rejected_fired_{false};  // debounce: fire once per rejection
    std::thread                 poll_thread_;

    CreditCallback              credit_cb_;
    FaultCallback               fault_cb_;

    // Channel value table populated by parseSetupRequest (or VND fallback)
    uint32_t channel_value_[kMaxChannels]{};
    char     currency_[4]               = {'V', 'N', 'D', '\0'};
    uint32_t value_multiplier_          = 1;
};
