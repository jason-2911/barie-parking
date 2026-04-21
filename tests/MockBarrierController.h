/**
 * @file MockBarrierController.h
 * @brief In-memory IBarrierController for self-test harness.
 *
 * Records every call and returns deterministic results so the test runner
 * can assert on exact interactions without touching a real RS485 bus.
 */

#pragma once

#include "hardware/IBarrierController.h"

#include <QVector>
#include <utility>

class MockBarrierController final : public IBarrierController {
public:
    // ── Scripting: what scan() should return each time it is called.
    void setScanResult(QVector<uint8_t> addrs) { scan_result_ = std::move(addrs); }

    // ── Inspection: what the harness called.
    int              scanCalls()     const { return scan_calls_; }
    QVector<std::pair<uint8_t, uint8_t>> setAddrCalls() const { return setaddr_calls_; }
    QVector<uint8_t> raiseCalls()    const { return raise_calls_; }
    QVector<uint8_t> lowerCalls()    const { return lower_calls_; }

    // ── IBarrierController ──────────────────────────────────────────────
    BarrierDriverError open()  override { is_open_ = true;  return BarrierDriverError::OK; }
    void               close() override { is_open_ = false; }
    bool               isOpen() const override { return is_open_; }

    QVector<uint8_t> scan(uint8_t /*start*/, uint8_t /*end*/) override {
        ++scan_calls_;
        return scan_result_;
    }
    void addSlave(uint8_t addr) override {
        if (!slaves_.contains(addr)) slaves_.append(addr);
    }

    BarrierDriverError setSlaveAddress(uint8_t cur, uint8_t neu) override {
        setaddr_calls_.append({cur, neu});
        if (cur < 1 || cur > 247 || neu < 1 || neu > 247)
            return BarrierDriverError::INVALID_ARG;
        // Scripted failure hook.
        if (fail_setaddr_) return BarrierDriverError::IO_ERROR;
        slaves_.removeAll(cur);
        if (!slaves_.contains(neu)) slaves_.append(neu);
        // Reflect the new addr in the next scan result so the UI can
        // re-render the list after a rewrite.
        for (auto& a : scan_result_) if (a == cur) a = neu;
        return BarrierDriverError::OK;
    }
    void setFailSetAddr(bool fail) { fail_setaddr_ = fail; }

    BarrierDriverError raise(uint8_t a) override { raise_calls_.append(a); return BarrierDriverError::OK; }
    BarrierDriverError lower(uint8_t a) override { lower_calls_.append(a); return BarrierDriverError::OK; }
    BarrierDriverError pause(uint8_t /*a*/) override { return BarrierDriverError::OK; }

    BarrierDriverError readStatus(uint8_t, BarrierDriverStatus&) override {
        return BarrierDriverError::OK;
    }
    BarrierDriverError readPosition(uint8_t, BarrierDriverPosition& out) override {
        out = BarrierDriverPosition::Up;
        return BarrierDriverError::OK;
    }
    void setPositionCallback(PositionCallback cb) override { event_cb_ = std::move(cb); }

private:
    bool              is_open_       = false;
    bool              fail_setaddr_  = false;
    int               scan_calls_    = 0;
    QVector<uint8_t>  scan_result_;
    QVector<uint8_t>  slaves_;
    QVector<std::pair<uint8_t, uint8_t>> setaddr_calls_;
    QVector<uint8_t>  raise_calls_;
    QVector<uint8_t>  lower_calls_;
    PositionCallback  event_cb_;
};
