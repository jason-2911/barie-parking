/**
 * BarrierControllerModbus.cpp
 * On macOS (__APPLE__): serial I/O is stubbed — just logs so the demo
 *                       can run without hardware.
 * On Linux: real POSIX termios RS485 Modbus RTU implementation.
 */

#include "hardware/drivers/BarrierControllerModbus.h"

#include <QDebug>
#include <chrono>
#include <cstring>
#include <cerrno>

#ifndef __APPLE__
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#endif

// ─── CRC16 (shared by both platforms) ────────────────────────────────────────

uint16_t BarrierControllerModbus::crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) crc = (crc >> 1) ^ 0xA001;
            else         crc >>= 1;
        }
    }
    return crc;
}

BarrierDriverPosition
BarrierControllerModbus::parsePosition(uint16_t status_word) {
    bool running = status_word & STATUS_BIT_RUNNING;
    bool up_dir  = status_word & STATUS_BIT_DIRECTION;
    if (status_word & STATUS_BIT_UP_POS)   return BarrierDriverPosition::Up;
    if (status_word & STATUS_BIT_DOWN_POS) return BarrierDriverPosition::Down;
    if (running)
        return up_dir ? BarrierDriverPosition::MovingUp
                      : BarrierDriverPosition::MovingDown;
    return BarrierDriverPosition::Unknown;
}

// ─── Constructor / Destructor ────────────────────────────────────────────────

BarrierControllerModbus::BarrierControllerModbus(const QString& port, int baud)
    : port_(port), baud_(baud) {}

BarrierControllerModbus::~BarrierControllerModbus() { close(); }

// ═════════════════════════════════════════════════════════════════════════════
// Platform-specific sections
// ═════════════════════════════════════════════════════════════════════════════

#ifdef __APPLE__

// ─── macOS STUB ─────────────────────────────────────────────────────────────

BarrierDriverError BarrierControllerModbus::open() {
    qInfo().nospace() << "[Barrier][STUB] open(port=" << port_
                      << ", baud=" << baud_ << ")";
    fd_ = 999;   // pretend-open sentinel
    return BarrierDriverError::OK;
}

void BarrierControllerModbus::close() {
    if (fd_ >= 0) {
        qInfo() << "[Barrier][STUB] close()";
        fd_ = -1;
    }
}

bool BarrierControllerModbus::isOpen() const { return fd_ >= 0; }

QVector<uint8_t> BarrierControllerModbus::scan(uint8_t start_addr,
                                                uint8_t end_addr) {
    qInfo().nospace() << "[Barrier][STUB] scan(" << start_addr
                      << ".." << end_addr << ") → returning empty list";
    return {};
}

void BarrierControllerModbus::addSlave(uint8_t addr) {
    if (!slaves_.contains(addr))
        slaves_.append(addr);
    qInfo() << "[Barrier][STUB] addSlave(" << addr << ")";
}

BarrierDriverError
BarrierControllerModbus::setSlaveAddress(uint8_t current_addr, uint8_t new_addr) {
    qInfo().nospace() << "[Barrier][STUB] setSlaveAddress("
                       << current_addr << " → " << new_addr << ")";
    slaves_.removeAll(current_addr);
    if (!slaves_.contains(new_addr)) slaves_.append(new_addr);
    return BarrierDriverError::OK;
}

BarrierDriverError BarrierControllerModbus::raise(uint8_t slave) {
    qInfo() << "[Barrier][STUB] RAISE slave=" << slave;
    if (event_cb_) event_cb_(slave, BarrierDriverPosition::Up);
    return BarrierDriverError::OK;
}

BarrierDriverError BarrierControllerModbus::lower(uint8_t slave) {
    qInfo() << "[Barrier][STUB] LOWER slave=" << slave;
    if (event_cb_) event_cb_(slave, BarrierDriverPosition::Down);
    return BarrierDriverError::OK;
}

BarrierDriverError BarrierControllerModbus::pause(uint8_t slave) {
    qInfo() << "[Barrier][STUB] PAUSE slave=" << slave;
    return BarrierDriverError::OK;
}

BarrierDriverError
BarrierControllerModbus::readStatus(uint8_t slave, BarrierDriverStatus& out) {
    out.slave_addr = slave;
    out.power_ok = true;
    out.position = BarrierDriverPosition::Down;
    return BarrierDriverError::OK;
}

BarrierDriverError
BarrierControllerModbus::readPosition(uint8_t slave,
                                       BarrierDriverPosition& out) {
    out = BarrierDriverPosition::Down;
    return BarrierDriverError::OK;
}

BarrierDriverError
BarrierControllerModbus::serialWrite(const uint8_t*, size_t) {
    return BarrierDriverError::OK;
}
BarrierDriverError
BarrierControllerModbus::serialRead(uint8_t*, size_t, int, size_t& read_out) {
    read_out = 0;
    return BarrierDriverError::TIMEOUT;
}
void BarrierControllerModbus::serialFlush() {}

BarrierDriverError
BarrierControllerModbus::sendCoil(uint8_t, uint16_t, bool) {
    return BarrierDriverError::OK;
}
BarrierDriverError
BarrierControllerModbus::sendReadRegs(uint8_t, uint16_t, uint16_t,
                                       QVector<uint16_t>&) {
    return BarrierDriverError::OK;
}

#else  // ═════════════ Linux (real implementation) ════════════════════════════

static speed_t baud_to_speed(int baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 115200: return B115200;
        default:     return B9600;
    }
}

BarrierDriverError BarrierControllerModbus::open() {
    if (fd_ >= 0) return BarrierDriverError::OK;

    fd_ = ::open(port_.toLocal8Bit().constData(),
                 O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        qCritical() << "[Barrier] cannot open" << port_ << ":" << strerror(errno);
        return BarrierDriverError::IO_ERROR;
    }

    struct termios tty{};
    if (tcgetattr(fd_, &tty) != 0) { ::close(fd_); fd_ = -1; return BarrierDriverError::IO_ERROR; }

    speed_t spd = baud_to_speed(baud_);
    cfsetispeed(&tty, spd);
    cfsetospeed(&tty, spd);
    tty.c_cflag &= ~PARENB; tty.c_cflag &= ~CSTOPB; tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8; tty.c_cflag &= ~CRTSCTS; tty.c_cflag |= CREAD | CLOCAL;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag &= ~(OPOST | ONLCR);
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG);
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        ::close(fd_); fd_ = -1;
        return BarrierDriverError::IO_ERROR;
    }
    tcflush(fd_, TCIOFLUSH);
    qInfo() << "[Barrier] opened" << port_ << "@" << baud_ << "baud";
    return BarrierDriverError::OK;
}

void BarrierControllerModbus::close() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

bool BarrierControllerModbus::isOpen() const { return fd_ >= 0; }

BarrierDriverError
BarrierControllerModbus::serialWrite(const uint8_t* data, size_t len) {
    if (fd_ < 0) return BarrierDriverError::NOT_OPEN;
    ssize_t w = ::write(fd_, data, len);
    if (w < 0 || (size_t)w != len) return BarrierDriverError::IO_ERROR;
    tcdrain(fd_);
    return BarrierDriverError::OK;
}

BarrierDriverError
BarrierControllerModbus::serialRead(uint8_t* buf, size_t len,
                                     int timeout_ms, size_t& read_out) {
    if (fd_ < 0) return BarrierDriverError::NOT_OPEN;
    read_out = 0;
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);

    while (read_out < len) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return BarrierDriverError::TIMEOUT;

        auto remaining_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                deadline - now).count();
        fd_set rfds; FD_ZERO(&rfds); FD_SET(fd_, &rfds);
        struct timeval tv{0, static_cast<suseconds_t>(remaining_us)};
        int sel = select(fd_ + 1, &rfds, nullptr, nullptr, &tv);
        if (sel <= 0) return BarrierDriverError::TIMEOUT;

        ssize_t r = ::read(fd_, buf + read_out, len - read_out);
        if (r > 0) read_out += r;
        else if (r < 0 && errno != EAGAIN) return BarrierDriverError::IO_ERROR;
    }
    return BarrierDriverError::OK;
}

void BarrierControllerModbus::serialFlush() {
    if (fd_ >= 0) tcflush(fd_, TCIOFLUSH);
}

BarrierDriverError
BarrierControllerModbus::sendCoil(uint8_t slave, uint16_t coil, bool on) {
    std::lock_guard<std::mutex> lock(bus_mutex_);
    uint8_t frame[8];
    frame[0] = slave;
    frame[1] = MB_FC_WRITE_SINGLE_COIL;
    frame[2] = coil >> 8;
    frame[3] = coil & 0xFF;
    frame[4] = on ? 0xFF : 0x00;
    frame[5] = 0x00;
    uint16_t crc = crc16(frame, 6);
    frame[6] = crc & 0xFF;
    frame[7] = crc >> 8;

    serialFlush();
    auto err = serialWrite(frame, 8);
    if (err != BarrierDriverError::OK) return err;

    uint8_t resp[8]; size_t got = 0;
    err = serialRead(resp, 8, 500, got);
    if (err != BarrierDriverError::OK || got != 8) return err;
    return BarrierDriverError::OK;
}

BarrierDriverError
BarrierControllerModbus::sendReadRegs(uint8_t slave, uint16_t start,
                                       uint16_t count,
                                       QVector<uint16_t>& out) {
    std::lock_guard<std::mutex> lock(bus_mutex_);
    uint8_t frame[8];
    frame[0] = slave;
    frame[1] = MB_FC_READ_HOLDING;
    frame[2] = start >> 8;
    frame[3] = start & 0xFF;
    frame[4] = count >> 8;
    frame[5] = count & 0xFF;
    uint16_t crc = crc16(frame, 6);
    frame[6] = crc & 0xFF;
    frame[7] = crc >> 8;

    serialFlush();
    auto err = serialWrite(frame, 8);
    if (err != BarrierDriverError::OK) return err;

    uint8_t header[3]; size_t got = 0;
    err = serialRead(header, 3, 500, got);
    if (err != BarrierDriverError::OK || got != 3) return err;
    uint8_t nbytes = header[2];
    QVector<uint8_t> body(nbytes + 2);
    err = serialRead(body.data(), nbytes + 2, 500, got);
    if (err != BarrierDriverError::OK) return err;

    out.clear();
    for (int i = 0; i < nbytes; i += 2)
        out.append(((uint16_t)body[i] << 8) | body[i + 1]);
    return BarrierDriverError::OK;
}

QVector<uint8_t>
BarrierControllerModbus::scan(uint8_t start_addr, uint8_t end_addr) {
    slaves_.clear();
    for (uint8_t a = start_addr; a <= end_addr; ++a) {
        QVector<uint16_t> out;
        if (sendReadRegs(a, REG_RUN_STATUS, 1, out) == BarrierDriverError::OK)
            slaves_.append(a);
    }
    return slaves_;
}

void BarrierControllerModbus::addSlave(uint8_t addr) {
    if (!slaves_.contains(addr)) slaves_.append(addr);
}

BarrierDriverError
BarrierControllerModbus::setSlaveAddress(uint8_t current_addr, uint8_t new_addr)
{
    if (current_addr < 1 || current_addr > 247 ||
        new_addr     < 1 || new_addr     > 247) {
        return BarrierDriverError::INVALID_ARG;
    }
    if (!isOpen()) return BarrierDriverError::NOT_OPEN;

    std::lock_guard<std::mutex> lock(bus_mutex_);

    // FC06: slave, FC, reg_hi, reg_lo, val_hi, val_lo, crc_lo, crc_hi
    uint8_t frame[8];
    frame[0] = current_addr;
    frame[1] = MB_FC_WRITE_REGISTER;
    frame[2] = static_cast<uint8_t>(REG_SLAVE_ADDR >> 8);
    frame[3] = static_cast<uint8_t>(REG_SLAVE_ADDR & 0xFF);
    frame[4] = 0x00;
    frame[5] = new_addr;
    uint16_t crc = crc16(frame, 6);
    frame[6] = static_cast<uint8_t>(crc & 0xFF);
    frame[7] = static_cast<uint8_t>(crc >> 8);

    serialFlush();
    auto err = serialWrite(frame, 8);
    if (err != BarrierDriverError::OK) return err;

    // FC06 echoes the request verbatim on success. Give the device a little
    // longer than usual since writing to the config register can trigger a
    // NVRAM flush on some firmwares.
    uint8_t resp[8]; size_t got = 0;
    err = serialRead(resp, 8, 800, got);
    if (err != BarrierDriverError::OK) return err;
    if (got != 8) return BarrierDriverError::TIMEOUT;

    // Refresh our slave list so subsequent ops target the new address.
    slaves_.removeAll(current_addr);
    if (!slaves_.contains(new_addr)) slaves_.append(new_addr);
    return BarrierDriverError::OK;
}

BarrierDriverError BarrierControllerModbus::raise(uint8_t slave) {
    return sendCoil(slave, COIL_UP, true);
}
BarrierDriverError BarrierControllerModbus::lower(uint8_t slave) {
    return sendCoil(slave, COIL_DOWN, true);
}
BarrierDriverError BarrierControllerModbus::pause(uint8_t slave) {
    return sendCoil(slave, COIL_PAUSE, true);
}

BarrierDriverError
BarrierControllerModbus::readStatus(uint8_t slave, BarrierDriverStatus& out) {
    QVector<uint16_t> regs;
    auto err = sendReadRegs(slave, REG_RUN_STATUS, 2, regs);
    if (err != BarrierDriverError::OK) return err;

    uint16_t status = regs[0];
    uint16_t fault  = regs.size() > 1 ? regs[1] : 0;

    out.slave_addr    = slave;
    out.is_running    = status & STATUS_BIT_RUNNING;
    out.direction_up  = status & STATUS_BIT_DIRECTION;
    out.position      = parsePosition(status);
    out.fault_code    = fault;
    out.power_ok      = !(fault);
    return BarrierDriverError::OK;
}

BarrierDriverError
BarrierControllerModbus::readPosition(uint8_t slave,
                                       BarrierDriverPosition& out) {
    QVector<uint16_t> regs;
    auto err = sendReadRegs(slave, REG_RUN_STATUS, 1, regs);
    if (err != BarrierDriverError::OK) return err;
    out = parsePosition(regs[0]);
    return BarrierDriverError::OK;
}

#endif  // __APPLE__
