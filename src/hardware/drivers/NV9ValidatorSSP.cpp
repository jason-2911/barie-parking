/**
 * NV9ValidatorSSP.cpp
 * macOS (__APPLE__): fully stubbed — `enable()` starts a timer that
 *                    produces fake 10k/20k VND credits so cash-flow demos
 *                    can be exercised without hardware.
 * Linux: skeleton of POSIX termios + SSP polling — fill in command
 *        dispatch with your full eSSP payloads.
 */

#include "hardware/drivers/NV9ValidatorSSP.h"

#include <QDebug>
#include <chrono>
#include <cstring>
#include <cerrno>

#ifndef __APPLE__
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#endif

// CRC-16 CCITT (poly 0x8005, init 0xFFFF) — SSP spec
uint16_t NV9ValidatorSSP::crc16Ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; ++j)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x8005 : crc << 1;
    }
    return crc;
}

// ─── Constructor / Destructor ────────────────────────────────────────────────

NV9ValidatorSSP::NV9ValidatorSSP(const QString& port, uint8_t slave_addr)
    : port_(port), slave_addr_(slave_addr) {}

NV9ValidatorSSP::~NV9ValidatorSSP() { close(); }

// ═════════════════════════════════════════════════════════════════════════════

#ifdef __APPLE__

// ─── macOS STUB ─────────────────────────────────────────────────────────────

NV9DriverError NV9ValidatorSSP::open() {
    qInfo().nospace() << "[NV9][STUB] open(port=" << port_
                      << ", slave=" << slave_addr_ << ")";
    fd_ = 999;
    state_ = NV9DriverState::Disabled;
    return NV9DriverError::OK;
}

void NV9ValidatorSSP::close() {
    stopPolling();
    if (fd_ >= 0) {
        qInfo() << "[NV9][STUB] close()";
        fd_ = -1;
    }
}

bool NV9ValidatorSSP::isOpen() const { return fd_ >= 0; }

NV9DriverError NV9ValidatorSSP::enable() {
    qInfo() << "[NV9][STUB] enable()";
    state_ = NV9DriverState::Enabled;
    return NV9DriverError::OK;
}

NV9DriverError NV9ValidatorSSP::disable() {
    qInfo() << "[NV9][STUB] disable()";
    state_ = NV9DriverState::Disabled;
    return NV9DriverError::OK;
}

NV9DriverError NV9ValidatorSSP::rejectNote() {
    qInfo() << "[NV9][STUB] rejectNote()";
    return NV9DriverError::OK;
}

NV9DriverError NV9ValidatorSSP::holdNote() {
    qInfo() << "[NV9][STUB] holdNote()";
    return NV9DriverError::OK;
}

NV9DriverError NV9ValidatorSSP::reset() {
    qInfo() << "[NV9][STUB] reset()";
    state_ = NV9DriverState::Idle;
    return NV9DriverError::OK;
}

void NV9ValidatorSSP::startPolling() {
    if (polling_.exchange(true)) return;
    qInfo() << "[NV9][STUB] startPolling() — thread disabled on macOS";
    // NOTE: no fake credits here. Use keyboard shortcut 'C' in the
    // client, or MQTT demo flow from server.py to exercise cash flow.
}

void NV9ValidatorSSP::stopPolling() {
    if (!polling_.exchange(false)) return;
    qInfo() << "[NV9][STUB] stopPolling()";
}

NV9DriverError NV9ValidatorSSP::serialWriteRaw(const uint8_t*, size_t) {
    return NV9DriverError::OK;
}
NV9DriverError NV9ValidatorSSP::serialReadRaw(uint8_t*, size_t, int, size_t& r) {
    r = 0;
    return NV9DriverError::TIMEOUT;
}
void NV9ValidatorSSP::serialFlush() {}
void NV9ValidatorSSP::pollThreadFunc() {}

#else  // ═════════════ Linux (real implementation skeleton) ══════════════════

static speed_t nv9_baud_to_speed(int baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 115200: return B115200;
        default:     return B9600;
    }
}

NV9DriverError NV9ValidatorSSP::open() {
    if (fd_ >= 0) return NV9DriverError::OK;
    fd_ = ::open(port_.toLocal8Bit().constData(),
                 O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        qCritical() << "[NV9] cannot open" << port_ << ":" << strerror(errno);
        return NV9DriverError::IO_ERROR;
    }
    struct termios tty{};
    tcgetattr(fd_, &tty);
    speed_t spd = nv9_baud_to_speed(9600);
    cfsetispeed(&tty, spd); cfsetospeed(&tty, spd);
    tty.c_cflag &= ~PARENB; tty.c_cflag &= ~CSTOPB; tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8 | CREAD | CLOCAL;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL | INLCR | IGNCR | ISTRIP);
    tty.c_oflag &= ~OPOST;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;
    tcsetattr(fd_, TCSANOW, &tty);
    tcflush(fd_, TCIOFLUSH);

    state_ = NV9DriverState::Disabled;
    qInfo() << "[NV9] opened" << port_;

    // TODO: implement full SSP sync + reset + setup_request sequence here.
    return NV9DriverError::OK;
}

void NV9ValidatorSSP::close() {
    stopPolling();
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

bool NV9ValidatorSSP::isOpen() const { return fd_ >= 0; }

NV9DriverError NV9ValidatorSSP::enable() {
    qInfo() << "[NV9] enable() — TODO send SSP_CMD_ENABLE";
    state_ = NV9DriverState::Enabled;
    return NV9DriverError::OK;
}

NV9DriverError NV9ValidatorSSP::disable() {
    qInfo() << "[NV9] disable() — TODO send SSP_CMD_DISABLE";
    state_ = NV9DriverState::Disabled;
    return NV9DriverError::OK;
}

NV9DriverError NV9ValidatorSSP::rejectNote() {
    qInfo() << "[NV9] rejectNote() — TODO send SSP_CMD_REJECT_BANKNOTE";
    return NV9DriverError::OK;
}

NV9DriverError NV9ValidatorSSP::holdNote() {
    qInfo() << "[NV9] holdNote() — TODO send SSP_CMD_HOLD";
    return NV9DriverError::OK;
}

NV9DriverError NV9ValidatorSSP::reset() {
    qInfo() << "[NV9] reset() — TODO send SSP_CMD_RESET";
    state_ = NV9DriverState::Idle;
    return NV9DriverError::OK;
}

void NV9ValidatorSSP::startPolling() {
    if (polling_.exchange(true)) return;
    poll_thread_ = std::thread(&NV9ValidatorSSP::pollThreadFunc, this);
}

void NV9ValidatorSSP::stopPolling() {
    if (!polling_.exchange(false)) return;
    if (poll_thread_.joinable()) poll_thread_.join();
}

void NV9ValidatorSSP::pollThreadFunc() {
    using namespace std::chrono;
    while (polling_.load()) {
        // TODO: send SSP_CMD_POLL and dispatch events via credit_cb_ / fault_cb_.
        std::this_thread::sleep_for(milliseconds(200));
    }
}

NV9DriverError
NV9ValidatorSSP::serialWriteRaw(const uint8_t* data, size_t len) {
    if (fd_ < 0) return NV9DriverError::NOT_OPEN;
    ssize_t w = ::write(fd_, data, len);
    return (w < 0 || (size_t)w != len) ? NV9DriverError::IO_ERROR
                                         : NV9DriverError::OK;
}

NV9DriverError
NV9ValidatorSSP::serialReadRaw(uint8_t* buf, size_t max_len,
                                int timeout_ms, size_t& read_out) {
    if (fd_ < 0) return NV9DriverError::NOT_OPEN;
    read_out = 0;
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);
    while (read_out < max_len) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return NV9DriverError::TIMEOUT;
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                      deadline - now).count();
        fd_set rfds; FD_ZERO(&rfds); FD_SET(fd_, &rfds);
        struct timeval tv{0, static_cast<suseconds_t>(us)};
        int sel = select(fd_ + 1, &rfds, nullptr, nullptr, &tv);
        if (sel <= 0) return NV9DriverError::TIMEOUT;
        ssize_t r = ::read(fd_, buf + read_out, max_len - read_out);
        if (r > 0) { read_out += r; break; }
        else if (r < 0 && errno != EAGAIN) return NV9DriverError::IO_ERROR;
    }
    return NV9DriverError::OK;
}

void NV9ValidatorSSP::serialFlush() {
    if (fd_ >= 0) tcflush(fd_, TCIOFLUSH);
}

#endif  // __APPLE__
