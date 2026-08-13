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
#define REG_RUN_STATUS           0xD014    // 监控参数14 运行状态 (2 bytes)
#define REG_FAULT_CODE           0xD015    // 监控参数15 故障码   (2 bytes, 0-6)
#define REG_PORT_INPUT           0xD016    // 监控参数16 端口输入状态 (V3.00+)

// Configuration registers (FC06). Vendor mnemonics in the "H00-XX" menu:
//   H00-31 → slave address  actual Modbus register 0x0010 (confirmed on hardware)
//   H00-32 → baud rate      actual Modbus register 0x0020 (read-only ref, not written)
#define REG_SLAVE_ADDR           0x0010
#define REG_BAUD_RATE            0x0020

// ─── D014H run status bits ──────────────────────────────────────────────────
// Source: 通讯协议操作说明.docx — "运行状态标置位" tables.
// The prior revision had DOWN_POS/UP_POS off-by-one which broke position
// detection entirely (parsePosition always returned Unknown on real gear).
// Correct mapping, precise to the doc:
//   Low byte:  bit0=running, bit1=direction, bit2=power_down,
//              bit4=normal_on, bit5=self_tested, bit6=selftest_err
//   High byte: bit9=down_pos, bit10=up_pos, bit11=in_position,
//              bit12=green_led, bit13=red_led
#define STATUS_BIT_RUNNING       (1 << 0)   // Bit0 运行状态
#define STATUS_BIT_DIRECTION     (1 << 1)   // Bit1 运行方向
#define STATUS_BIT_POWER_DOWN    (1 << 2)   // Bit2 电源掉电
#define STATUS_BIT_NORMAL_ON     (1 << 4)   // Bit4 正常上电
#define STATUS_BIT_SELF_TESTED   (1 << 5)   // Bit5 已自检
#define STATUS_BIT_SELFTEST_ERR  (1 << 6)   // Bit6 自检错误
#define STATUS_BIT_DOWN_POS      (1 << 9)   // Bit9  下杆到位
#define STATUS_BIT_UP_POS        (1 << 10)  // Bit10 上杆到位
#define STATUS_BIT_IN_POSITION   (1 << 11)  // Bit11 到位输出 (either limit)
#define STATUS_BIT_GREEN_LIGHT   (1 << 12)  // Bit12 绿灯输出
#define STATUS_BIT_RED_LIGHT     (1 << 13)  // Bit13 红灯输出

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

    BarrierDriverError setSlaveAddress(uint8_t current_addr,
                                        uint8_t new_addr) override;

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
