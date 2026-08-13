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
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     return B9600;
    }
}

// Assert or de-assert RTS (DE/RE pin on MAX485 transceiver).
// Returns 0 on success, -1 on ioctl failure.
static int rtsSet(int fd, bool assert_rts) {
    int flags = 0;
    if (ioctl(fd, TIOCMGET, &flags) < 0) return -1;
    if (assert_rts) flags |= TIOCM_RTS;
    else            flags &= ~TIOCM_RTS;
    return ioctl(fd, TIOCMSET, &flags) < 0 ? -1 : 0;
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
    rtsSet(fd_, false);  // adapter starts in receive mode
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
BarrierControllerModbus::serialRead(uint8_t* buf, size_t max_len,
                                     int timeout_ms, size_t& read_out) {
    if (fd_ < 0) return BarrierDriverError::NOT_OPEN;
    read_out = 0;
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);

    while (read_out < max_len) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) break;

        auto remaining_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                deadline - now).count();
        fd_set rfds; FD_ZERO(&rfds); FD_SET(fd_, &rfds);
        struct timeval tv{0, static_cast<suseconds_t>(remaining_us)};
        int sel = select(fd_ + 1, &rfds, nullptr, nullptr, &tv);
        if (sel < 0 && errno == EINTR) continue;
        if (sel <= 0) break;

        ssize_t r = ::read(fd_, buf + read_out, max_len - read_out);
        if (r > 0) {
            read_out += (size_t)r;
            /*
             * Modbus RTU inter-frame gap: after receiving a chunk, wait up
             * to 10 ms for more bytes. Silence means the frame is complete.
             * This handles adapters that deliver the response in bursts and
             * avoids blocking for the full timeout on short frames.
             */
            struct timeval ift{0, 10000};
            fd_set rfd2; FD_ZERO(&rfd2); FD_SET(fd_, &rfd2);
            if (select(fd_ + 1, &rfd2, nullptr, nullptr, &ift) <= 0)
                break;  // no more bytes within gap — frame complete
        } else if (r < 0 && errno != EAGAIN) {
            return BarrierDriverError::IO_ERROR;
        }
    }
    return (read_out > 0) ? BarrierDriverError::OK : BarrierDriverError::TIMEOUT;
}

void BarrierControllerModbus::serialFlush() {
    if (fd_ >= 0) tcflush(fd_, TCIOFLUSH);
}

BarrierDriverError
BarrierControllerModbus::sendCoil(uint8_t slave, uint16_t coil, bool on) {
    std::lock_guard<std::mutex> lock(bus_mutex_);

    uint8_t req[8];
    req[0] = slave;
    req[1] = MB_FC_WRITE_SINGLE_COIL;
    req[2] = static_cast<uint8_t>(coil >> 8);
    req[3] = static_cast<uint8_t>(coil & 0xFF);
    req[4] = on ? 0xFF : 0x00;
    req[5] = 0x00;
    uint16_t crc = crc16(req, 6);
    req[6] = static_cast<uint8_t>(crc & 0xFF);
    req[7] = static_cast<uint8_t>(crc >> 8);

    serialFlush();

    rtsSet(fd_, true);
    usleep(1000);
    auto err = serialWrite(req, sizeof(req));
    tcdrain(fd_);
    rtsSet(fd_, false);
    usleep(1000);
    if (err != BarrierDriverError::OK) return err;

    uint8_t resp[8]; size_t got = 0;
    err = serialRead(resp, sizeof(resp), 500, got);
    if (err != BarrierDriverError::OK) return err;
    if (got < sizeof(req)) return BarrierDriverError::TIMEOUT;

    // CRC validation
    uint16_t crc_recv = (static_cast<uint16_t>(resp[got - 1]) << 8) | resp[got - 2];
    if (crc_recv != crc16(resp, got - 2)) return BarrierDriverError::CRC_MISMATCH;

    // Slave ID check
    if (resp[0] != slave) return BarrierDriverError::SLAVE_NOT_FOUND;

    // Modbus exception response (FC bit 7 set)
    if (resp[1] & 0x80) return BarrierDriverError::DEVICE_FAULT;

    // FC05 must echo the first 6 bytes of the request verbatim
    if (memcmp(req, resp, 6) != 0) return BarrierDriverError::IO_ERROR;

    return BarrierDriverError::OK;
}

BarrierDriverError
BarrierControllerModbus::sendReadRegs(uint8_t slave, uint16_t start,
                                       uint16_t count,
                                       QVector<uint16_t>& out) {
    std::lock_guard<std::mutex> lock(bus_mutex_);

    uint8_t req[8];
    req[0] = slave;
    req[1] = MB_FC_READ_HOLDING;
    req[2] = static_cast<uint8_t>(start >> 8);
    req[3] = static_cast<uint8_t>(start & 0xFF);
    req[4] = static_cast<uint8_t>(count >> 8);
    req[5] = static_cast<uint8_t>(count & 0xFF);
    uint16_t crc = crc16(req, 6);
    req[6] = static_cast<uint8_t>(crc & 0xFF);
    req[7] = static_cast<uint8_t>(crc >> 8);

    serialFlush();

    rtsSet(fd_, true);
    usleep(1000);
    auto err = serialWrite(req, sizeof(req));
    tcdrain(fd_);
    rtsSet(fd_, false);
    usleep(1000);
    if (err != BarrierDriverError::OK) return err;

    // Response: addr(1) + FC(1) + byte_count(1) + data(2*count) + CRC(2)
    const size_t resp_len = 5 + static_cast<size_t>(count) * 2;
    QVector<uint8_t> resp(static_cast<int>(resp_len));
    size_t got = 0;
    err = serialRead(resp.data(), resp_len, 500, got);
    if (err != BarrierDriverError::OK) return err;
    if (got < 5) return BarrierDriverError::TIMEOUT;

    // CRC validation
    uint16_t crc_recv = (static_cast<uint16_t>(resp[got - 1]) << 8) | resp[got - 2];
    if (crc_recv != crc16(resp.data(), got - 2)) return BarrierDriverError::CRC_MISMATCH;

    // Slave ID check
    if (resp[0] != slave) return BarrierDriverError::SLAVE_NOT_FOUND;

    // Modbus exception response (FC bit 7 set)
    if (resp[1] & 0x80) return BarrierDriverError::DEVICE_FAULT;

    // Byte-count field validation
    uint8_t byte_count = resp[2];
    if (byte_count != static_cast<uint8_t>(count * 2)) return BarrierDriverError::IO_ERROR;
    if (got < 3u + byte_count + 2u) return BarrierDriverError::IO_ERROR;

    out.clear();
    for (uint16_t i = 0; i < count; i++)
        out.append((static_cast<uint16_t>(resp[3 + i * 2]) << 8) | resp[4 + i * 2]);

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

    // FC06 request goes out addressed to current_addr.
    uint8_t req[8];
    req[0] = current_addr;
    req[1] = MB_FC_WRITE_REGISTER;
    req[2] = static_cast<uint8_t>(REG_SLAVE_ADDR >> 8);
    req[3] = static_cast<uint8_t>(REG_SLAVE_ADDR & 0xFF);
    req[4] = 0x00;
    req[5] = new_addr;
    uint16_t crc = crc16(req, 6);
    req[6] = static_cast<uint8_t>(crc & 0xFF);
    req[7] = static_cast<uint8_t>(crc >> 8);

    serialFlush();

    rtsSet(fd_, true);
    usleep(1000);
    auto err = serialWrite(req, sizeof(req));
    tcdrain(fd_);
    rtsSet(fd_, false);
    usleep(1000);
    if (err != BarrierDriverError::OK) return err;

    // This firmware applies the new address immediately and echoes FC06 with
    // new_addr in byte[0] (not current_addr). Give extra time for NVRAM flush.
    uint8_t resp[8]; size_t got = 0;
    err = serialRead(resp, sizeof(resp), 800, got);
    if (err != BarrierDriverError::OK) return err;
    if (got < sizeof(req)) return BarrierDriverError::TIMEOUT;

    // CRC validation
    uint16_t crc_recv = (static_cast<uint16_t>(resp[7]) << 8) | resp[6];
    if (crc_recv != crc16(resp, 6)) return BarrierDriverError::CRC_MISMATCH;

    // Device responds with the new address in byte[0]
    if (resp[0] != new_addr) return BarrierDriverError::SLAVE_NOT_FOUND;
    if (resp[1] & 0x80) return BarrierDriverError::DEVICE_FAULT;

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
