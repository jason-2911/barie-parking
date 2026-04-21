/**
 * BarrierControllerModbus.h
 * Modbus RTU RS485 Barrier Gate Controller — implements IBarrierController.
 *
 * Supports multi-barrier control over a single RS485 bus.
 * Protocol: Modbus RTU, FC 03 (read), 05 (coil), 06 (write register)
 *
 * On Apple/macOS builds the serial I/O is stubbed — all public methods
 * just log so the UI demo can run without physical hardware.
 */

#pragma once

#include "hardware/IBarrierController.h"

#include <QString>
#include <QVector>
#include <mutex>

// Modbus Function Codes
#define MB_FC_READ_HOLDING       0x03
#define MB_FC_WRITE_SINGLE_COIL  0x05
#define MB_FC_WRITE_REGISTER     0x06

// Coil addresses (FC05)
#define COIL_UP                  0x0000
#define COIL_DOWN                0x0001
#define COIL_PAUSE               0x0002
#define COIL_SELF_TEST           0x0003

// Monitor registers (FC03, base 0xD000)
#define REG_RUN_STATUS           0xD014
#define REG_FAULT_CODE           0xD015

// Run status bits
#define STATUS_BIT_RUNNING       (1 << 0)
#define STATUS_BIT_DIRECTION     (1 << 1)
#define STATUS_BIT_DOWN_POS      (1 << 8)
#define STATUS_BIT_UP_POS        (1 << 9)

class BarrierControllerModbus : public IBarrierController {
public:
    BarrierControllerModbus(const QString& port, int baud = 9600);
    ~BarrierControllerModbus() override;

    BarrierControllerModbus(const BarrierControllerModbus&) = delete;
    BarrierControllerModbus& operator=(const BarrierControllerModbus&) = delete;

    // ── IBarrierController ──────────────────────────────────────────────
    BarrierDriverError open() override;
    void close() override;
    bool isOpen() const override;

    QVector<uint8_t> scan(uint8_t start_addr = 1,
                           uint8_t end_addr = 32) override;
    void addSlave(uint8_t addr) override;

    BarrierDriverError raise(uint8_t slave_addr) override;
    BarrierDriverError lower(uint8_t slave_addr) override;
    BarrierDriverError pause(uint8_t slave_addr) override;

    BarrierDriverError readStatus(uint8_t slave_addr,
                                   BarrierDriverStatus& out) override;
    BarrierDriverError readPosition(uint8_t slave_addr,
                                     BarrierDriverPosition& out) override;

    void setPositionCallback(PositionCallback cb) override { event_cb_ = cb; }

private:
    // Serial I/O (real on Linux, stubbed on __APPLE__)
    BarrierDriverError serialWrite(const uint8_t* data, size_t len);
    BarrierDriverError serialRead(uint8_t* buf, size_t len,
                                   int timeout_ms, size_t& read_out);
    void serialFlush();

    // Modbus framing
    static uint16_t crc16(const uint8_t* data, size_t len);
    BarrierDriverError sendCoil(uint8_t slave, uint16_t coil, bool on);
    BarrierDriverError sendReadRegs(uint8_t slave, uint16_t start_reg,
                                     uint16_t count,
                                     QVector<uint16_t>& out);
    static BarrierDriverPosition parsePosition(uint16_t status_word);

    QString            port_;
    int                baud_;
    int                fd_ = -1;
    std::mutex         bus_mutex_;
    QVector<uint8_t>   slaves_;
    PositionCallback   event_cb_;
};
