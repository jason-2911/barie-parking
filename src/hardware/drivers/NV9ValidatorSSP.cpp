/**
 * @file NV9ValidatorSSP.cpp
 * @brief ITL NV9 USB bill validator — eSSP driver implementation.
 *
 * macOS (__APPLE__): all public methods are logged stubs so the UI
 *                    can be exercised without hardware.
 * Linux:             full eSSP implementation ported from nv9_ssp.c.
 *                    Reconnect-capable poll thread; credit / fault
 *                    callbacks are invoked from the poll thread
 *                    (Qt auto-queues cross-thread signals).
 */

#include "hardware/drivers/NV9ValidatorSSP.h"

#include <QDebug>
#include <cerrno>
#include <chrono>
#include <cstring>

#ifndef __APPLE__
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

// ── CRC-16 CCITT (poly 0x8005, init 0xFFFF) — SSP spec ──────────────────────

uint16_t NV9ValidatorSSP::crc16Ccitt(const uint8_t* data, size_t len) noexcept
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int j = 0; j < 8; ++j)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x8005u)
                                  : (uint16_t)(crc << 1);
    }
    return crc;
}

// ── Constructor / Destructor ─────────────────────────────────────────────────

NV9ValidatorSSP::NV9ValidatorSSP(const QString& port, uint8_t slave_addr)
    : port_(port), slave_addr_(slave_addr) {}

NV9ValidatorSSP::~NV9ValidatorSSP() { close(); }

// ════════════════════════════════════════════════════════════════════════════════
#ifdef __APPLE__
// ── macOS STUB ───────────────────────────────────────────────────────────────

NV9DriverError NV9ValidatorSSP::open()
{
    qInfo().nospace() << "[NV9][STUB] open(port=" << port_
                      << " slave=0x" << Qt::hex << slave_addr_ << ")";
    fd_.store(999, std::memory_order_release);
    state_  = NV9DriverState::Disabled;
    return NV9DriverError::OK;
}

void NV9ValidatorSSP::close()
{
    stopPolling();
    if (fd_.load(std::memory_order_acquire) >= 0) {
        qInfo() << "[NV9][STUB] close()";
        fd_.store(-1, std::memory_order_release);
    }
}

bool NV9ValidatorSSP::isOpen() const
{
    return fd_.load(std::memory_order_acquire) >= 0;
}

NV9DriverError NV9ValidatorSSP::enable()
{
    qInfo() << "[NV9][STUB] enable()";
    state_ = NV9DriverState::Enabled;
    return NV9DriverError::OK;
}

NV9DriverError NV9ValidatorSSP::disable()
{
    qInfo() << "[NV9][STUB] disable()";
    state_ = NV9DriverState::Disabled;
    return NV9DriverError::OK;
}

NV9DriverError NV9ValidatorSSP::rejectNote()
{
    qInfo() << "[NV9][STUB] rejectNote()";
    return NV9DriverError::OK;
}

NV9DriverError NV9ValidatorSSP::holdNote()
{
    qInfo() << "[NV9][STUB] holdNote()";
    return NV9DriverError::OK;
}

NV9DriverError NV9ValidatorSSP::reset()
{
    qInfo() << "[NV9][STUB] reset()";
    state_ = NV9DriverState::Idle;
    return NV9DriverError::OK;
}

void NV9ValidatorSSP::startPolling()
{
    if (polling_.exchange(true)) return;
    qInfo() << "[NV9][STUB] startPolling() — thread disabled on macOS";
}

void NV9ValidatorSSP::stopPolling()
{
    if (!polling_.exchange(false)) return;
    qInfo() << "[NV9][STUB] stopPolling()";
}

// Stubs for private helpers — must be defined in both branches.
NV9DriverError NV9ValidatorSSP::openSerialPort()               { return NV9DriverError::OK; }
void           NV9ValidatorSSP::closeSerialPort()              {}
NV9DriverError NV9ValidatorSSP::serialWriteRaw(const uint8_t*, size_t)
                                                               { return NV9DriverError::OK; }
NV9DriverError NV9ValidatorSSP::serialReadRaw(uint8_t*, size_t, int, size_t& r)
                                                               { r = 0; return NV9DriverError::TIMEOUT; }
void           NV9ValidatorSSP::serialFlush()                  {}
int            NV9ValidatorSSP::buildFrame(const uint8_t*, int, uint8_t*, int) const noexcept
                                                               { return -ENOSPC; }
NV9DriverError NV9ValidatorSSP::sendCmdLocked(const char*, const uint8_t*, int,
                                               uint8_t*, int&){ return NV9DriverError::OK; }
NV9DriverError NV9ValidatorSSP::sendCmd(const char*, const uint8_t*, int,
                                         uint8_t*, int&)      { return NV9DriverError::OK; }
bool           NV9ValidatorSSP::isRespOk(const uint8_t*, int) noexcept
                                                               { return false; }
NV9DriverError NV9ValidatorSSP::initSequence()                 { return NV9DriverError::OK; }
void           NV9ValidatorSSP::parseSetupRequest(const uint8_t*, int) {}
int            NV9ValidatorSSP::parsePoll(const uint8_t*, int) { return 0; }
void           NV9ValidatorSSP::pollThreadFunc()               {}

#else  // ════════════════════════════ Linux ════════════════════════════════════

namespace {

// ── SSP command codes ─────────────────────────────────────────────────────────
constexpr uint8_t kCmdReset         = 0x01;
constexpr uint8_t kCmdSync          = 0x11;
constexpr uint8_t kCmdSetupRequest  = 0x05;
constexpr uint8_t kCmdChInhibits    = 0x02;
constexpr uint8_t kCmdEnable        = 0x0A;
constexpr uint8_t kCmdDisable       = 0x09;
constexpr uint8_t kCmdRejectNote    = 0x08;
constexpr uint8_t kCmdHold          = 0x18;
constexpr uint8_t kCmdPoll          = 0x07;

// ── SSP response / event codes ────────────────────────────────────────────────
constexpr uint8_t kRespOk           = 0xF0;
constexpr uint8_t kEvtSlaveReset    = 0xF1;
constexpr uint8_t kEvtNoteRead      = 0xEF;
constexpr uint8_t kEvtNoteCredit    = 0xEE;
constexpr uint8_t kEvtNoteRejected  = 0xED;
constexpr uint8_t kEvtNoteStacking  = 0xCC;
constexpr uint8_t kEvtNoteStacked   = 0xEB;
constexpr uint8_t kEvtNoteReturning = 0xEC;
constexpr uint8_t kEvtSafeJam       = 0xEA;
constexpr uint8_t kEvtUnsafeJam     = 0xE9;
constexpr uint8_t kEvtStackerFull   = 0xE7;
constexpr uint8_t kEvtDisabled      = 0xE8;
constexpr uint8_t kEvtInitializing  = 0xB6;

// ── Protocol byte ─────────────────────────────────────────────────────────────
constexpr uint8_t kStx              = 0x7F;

// ── Timing ───────────────────────────────────────────────────────────────────
constexpr int kPollIntervalMs    = 200;
constexpr int kReconnectDelayMs  = 2000;
constexpr int kInterCmdDelayMs   = 80;   // device processing time after TX
constexpr int kSendTimeoutMs     = 500;  // max wait for response
constexpr int kInitDelayMs       = 100;  // gap between init commands
constexpr int kReenableDelayMs   = 500;  // settle after slave reset
constexpr int kDtrRtsSettleMs    = 150;  // DTR/RTS assert settle time

} // anonymous namespace

// ── Serial port ───────────────────────────────────────────────────────────────

/**
 * @brief Open and configure the serial port (9600-8N1, blocking, DTR+RTS).
 *
 * Writes fd_ under io_mutex_ so concurrent sendCmd() calls see a
 * consistent fd_ value.
 *
 * @pre fd_ == -1
 */
NV9DriverError NV9ValidatorSSP::openSerialPort()
{
    int fd = ::open(port_.toLocal8Bit().constData(),
                    O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        qCritical() << "[NV9] cannot open" << port_ << ":" << strerror(errno);
        return NV9DriverError::IO_ERROR;
    }

    // Switch to blocking after open so we can set VTIME
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0) {
        ::close(fd);
        return NV9DriverError::IO_ERROR;
    }

    struct termios tty{};
    if (tcgetattr(fd, &tty) < 0) { ::close(fd); return NV9DriverError::IO_ERROR; }

    cfmakeraw(&tty);
    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);
    tty.c_cflag &= (tcflag_t)~(PARENB | CSTOPB | CSIZE);
    tty.c_cflag |= CS8 | CREAD | CLOCAL;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 10;  // 1 s inter-character timeout

    if (tcsetattr(fd, TCSANOW, &tty) < 0) { ::close(fd); return NV9DriverError::IO_ERROR; }

    // Assert DTR + RTS, wait for device to notice, flush garbage
    int mbits = TIOCM_DTR | TIOCM_RTS;
    ioctl(fd, TIOCMBIS, &mbits);
    std::this_thread::sleep_for(std::chrono::milliseconds(kDtrRtsSettleMs));
    tcflush(fd, TCIOFLUSH);

    // Store fd_ atomically — visible to both poll thread and main thread
    // without holding io_mutex_ (the atomic itself provides the ordering).
    fd_.store(fd, std::memory_order_release);
    return NV9DriverError::OK;
}

/**
 * @brief Close fd_ under io_mutex_.
 * @thread_safety Safe to call from the poll thread.
 */
void NV9ValidatorSSP::closeSerialPort()
{
    std::lock_guard<std::mutex> lock(io_mutex_);
    int fd = fd_.exchange(-1, std::memory_order_acq_rel);
    if (fd >= 0) ::close(fd);
}

NV9DriverError NV9ValidatorSSP::open()
{
    if (fd_.load(std::memory_order_acquire) >= 0) return NV9DriverError::OK;
    auto err = openSerialPort();
    if (err == NV9DriverError::OK) {
        state_ = NV9DriverState::Disabled;
        qInfo() << "[NV9] opened" << port_;
    }
    return err;
}

void NV9ValidatorSSP::close()
{
    stopPolling();       // join poll thread first
    closeSerialPort();
}

bool NV9ValidatorSSP::isOpen() const
{
    return fd_.load(std::memory_order_acquire) >= 0;
}

// ── Raw I/O ───────────────────────────────────────────────────────────────────

NV9DriverError NV9ValidatorSSP::serialWriteRaw(const uint8_t* data, size_t len)
{
    // Caller must hold io_mutex_; fd_ is stable for the duration of the lock.
    const int fd = fd_.load(std::memory_order_relaxed);
    if (fd < 0) return NV9DriverError::NOT_OPEN;
    ssize_t w = ::write(fd, data, len);
    return (w < 0 || (size_t)w != len) ? NV9DriverError::IO_ERROR
                                       : NV9DriverError::OK;
}

/**
 * @brief Read up to max_len bytes from fd_, waiting at most timeout_ms.
 *
 * Uses select() for non-blocking wait then a single read(). Caller loops
 * until a complete frame is accumulated (done in sendCmdLocked).
 */
NV9DriverError NV9ValidatorSSP::serialReadRaw(uint8_t* buf, size_t max_len,
                                               int timeout_ms, size_t& read_out)
{
    // Caller must hold io_mutex_; snapshot fd_ once under the lock.
    const int fd = fd_.load(std::memory_order_relaxed);
    if (fd < 0) return NV9DriverError::NOT_OPEN;
    read_out = 0;

    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);

    while (read_out < max_len) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return NV9DriverError::TIMEOUT;
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                      deadline - now).count();

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv{ 0, static_cast<suseconds_t>(us) };
        int sel = select(fd + 1, &rfds, nullptr, nullptr, &tv);
        if (sel <= 0) return NV9DriverError::TIMEOUT;

        ssize_t r = ::read(fd, buf + read_out, max_len - read_out);
        if (r > 0) { read_out += (size_t)r; break; }
        if (r < 0 && errno != EAGAIN) return NV9DriverError::IO_ERROR;
    }
    return NV9DriverError::OK;
}

void NV9ValidatorSSP::serialFlush()
{
    const int fd = fd_.load(std::memory_order_acquire);
    if (fd >= 0) tcflush(fd, TCIOFLUSH);
}

// ── SSP frame builder ─────────────────────────────────────────────────────────

/**
 * @brief Build a byte-stuffed SSP frame.
 *
 * Inner layout (before byte-stuffing):
 *   [seq<<7 | slave_addr] [payload_len] [payload...] [CRC_LO] [CRC_HI]
 *
 * Byte-stuffing: every 0x7F byte in the inner data is doubled in out[].
 * Prepends the leading STX (0x7F) which is never stuffed.
 *
 * @return Bytes written to out[], or negative errno on error.
 * @pre Caller holds io_mutex_
 */
int NV9ValidatorSSP::buildFrame(const uint8_t* payload, int payload_len,
                                uint8_t* out, int out_size) const noexcept
{
    if (payload_len < 0 || payload_len > kMaxFrameLen - 4) return -EINVAL;

    uint8_t inner[kMaxFrameLen];
    int     n = 0;

    inner[n++] = (uint8_t)((seq_bit_ << 7) | slave_addr_);
    inner[n++] = (uint8_t)payload_len;
    for (int i = 0; i < payload_len; i++) {
        if (n >= kMaxFrameLen) return -ENOSPC;
        inner[n++] = payload[i];
    }

    uint16_t crc = crc16Ccitt(inner, (size_t)n);
    if (n + 2 > kMaxFrameLen) return -ENOSPC;
    inner[n++] = (uint8_t)(crc & 0xFFu);
    inner[n++] = (uint8_t)(crc >> 8);

    // Build output: leading STX then byte-stuffed inner
    int out_len = 0;
    if (out_size < 1) return -ENOSPC;
    out[out_len++] = kStx;

    for (int i = 0; i < n; i++) {
        if (out_len + 2 > out_size) return -ENOSPC;
        out[out_len++] = inner[i];
        if (inner[i] == kStx) out[out_len++] = kStx;  // SSP spec §3.2
    }
    return out_len;
}

// ── SSP transceiver ───────────────────────────────────────────────────────────

bool NV9ValidatorSSP::isRespOk(const uint8_t* resp, int len) noexcept
{
    // resp[0]=STX resp[1]=seq resp[2]=len resp[3]=response_code
    return len >= 4 && resp[3] == kRespOk;
}

/**
 * @brief Write frame + read back response.
 * @pre Caller holds io_mutex_
 * @post seq_bit_ toggled on successful write.
 */
NV9DriverError NV9ValidatorSSP::sendCmdLocked(const char* name,
                                               const uint8_t* cmd, int cmd_len,
                                               uint8_t* resp, int& resp_len)
{
    uint8_t frame[kMaxFrameLen];
    int frame_len = buildFrame(cmd, cmd_len, frame, sizeof(frame));
    if (frame_len < 0) {
        qCritical() << "[NV9]" << name << "buildFrame failed:" << frame_len;
        return NV9DriverError::INVALID_ARG;
    }

    if (qstrcmp(name, "POLL") != 0) qDebug().nospace() << "[NV9] TX " << name;

    auto err = serialWriteRaw(frame, (size_t)frame_len);
    if (err != NV9DriverError::OK) {
        qWarning() << "[NV9]" << name << "write error";
        return err;
    }

    seq_bit_ ^= 1u;  // alternate after every successful write — SSP §4.2

    // Give the device time to process before reading
    std::this_thread::sleep_for(std::chrono::milliseconds(kInterCmdDelayMs));

    // Read until we have a complete frame or timeout
    size_t total    = 0;
    auto   deadline = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(kSendTimeoutMs);

    while (total < (size_t)kMaxFrameLen) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                             deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) break;

        size_t       n        = 0;
        NV9DriverError rd_err = serialReadRaw(resp + total,
                                               (size_t)kMaxFrameLen - total,
                                               (int)remaining, n);
        total += n;
        if (rd_err != NV9DriverError::OK && rd_err != NV9DriverError::TIMEOUT) {
            resp_len = (int)total;
            return rd_err;
        }

        // Complete frame: STX(1) + seq(1) + len(1) + data(len) + CRC(2)
        if (total >= 4 && total >= (size_t)(resp[2] + 5u)) break;
        if (n == 0) break;  // nothing more arriving
    }

    resp_len = (int)total;
    if (resp_len < 4) {
        qWarning().nospace() << "[NV9] " << name
                             << " timeout (got " << resp_len << " bytes)";
        return NV9DriverError::TIMEOUT;
    }
    if (qstrcmp(name, "POLL") != 0) qDebug().nospace() << "[NV9] RX " << name << " code=0x"
                                                        << Qt::hex << resp[3];
    return NV9DriverError::OK;
}

NV9DriverError NV9ValidatorSSP::sendCmd(const char* name,
                                         const uint8_t* cmd, int cmd_len,
                                         uint8_t* resp, int& resp_len)
{
    std::lock_guard<std::mutex> lock(io_mutex_);
    return sendCmdLocked(name, cmd, cmd_len, resp, resp_len);
}

// ── Init sequence ─────────────────────────────────────────────────────────────

/**
 * @brief Parse SETUP_REQUEST response and fill channel_value_[].
 *
 * Response data layout (bytes at offset +4, after STX/seq/len/F0):
 *   [0]    unit_type
 *   [1]    fw_major  [2] fw_minor
 *   [3..5] country_code (ASCII, e.g. "VND")
 *   [6..9] value_multiplier (uint32 LE)
 *   [10]   num_channels
 *   [11..] channel values, 4 bytes LE each
 *
 * @pre io_mutex_ held
 */
void NV9ValidatorSSP::parseSetupRequest(const uint8_t* resp, int len)
{
    if (!isRespOk(resp, len) || len < 16) return;

    const uint8_t* d     = resp + 4;
    int            avail = len - 6;  // exclude 4-byte header and 2-byte CRC
    if (avail < 11) return;

    memcpy(currency_, d + 3, 3);
    currency_[3] = '\0';

    value_multiplier_ = (uint32_t)d[6]
                      | ((uint32_t)d[7] <<  8)
                      | ((uint32_t)d[8] << 16)
                      | ((uint32_t)d[9] << 24);
    if (value_multiplier_ == 0) value_multiplier_ = 1;

    uint8_t num_ch = d[10];
    if (num_ch > kMaxChannels) num_ch = kMaxChannels;

    for (int i = 0; i < (int)num_ch; i++) {
        int off = 11 + i * 4;
        if (off + 3 >= avail) break;
        uint32_t raw = (uint32_t)d[off]
                     | ((uint32_t)d[off+1] <<  8)
                     | ((uint32_t)d[off+2] << 16)
                     | ((uint32_t)d[off+3] << 24);
        channel_value_[i] = raw * value_multiplier_;
    }

    qInfo().nospace() << "[NV9] FW: " << d[1] << "." << d[2]
                      << " | Currency: " << currency_
                      << " | Multiplier: " << value_multiplier_;
}

/**
 * @brief Full SSP handshake: SYNC → SETUP_REQUEST → CH_INHIBITS → ENABLE.
 *
 * Resets seq_bit_ = 0 at the start (per SSP spec).
 * Hard-coded VND channel table is used as fallback if SETUP_REQUEST
 * returns no usable channel data.
 *
 * @pre fd_ >= 0, io_mutex_ NOT held by caller
 */
NV9DriverError NV9ValidatorSSP::initSequence()
{
    std::lock_guard<std::mutex> lock(io_mutex_);
    seq_bit_ = 0;
    memset(channel_value_, 0, sizeof(channel_value_));

    uint8_t resp[kMaxFrameLen];
    int     rlen = 0;

    // SYNC
    const uint8_t cmd_sync[] = {kCmdSync};
    if (sendCmdLocked("SYNC", cmd_sync, 1, resp, rlen) != NV9DriverError::OK
            || !isRespOk(resp, rlen)) {
        qWarning() << "[NV9] SYNC failed";
        return NV9DriverError::PROTOCOL_ERROR;
    }

    // SETUP_REQUEST — informational, non-fatal if it fails
    const uint8_t cmd_setup[] = {kCmdSetupRequest};
    if (sendCmdLocked("SETUP_REQ", cmd_setup, 1, resp, rlen) == NV9DriverError::OK)
        parseSetupRequest(resp, rlen);
    std::this_thread::sleep_for(std::chrono::milliseconds(kInitDelayMs));

    // CH_INHIBITS — enable all 16 channels (0xFF 0xFF = channels 1–16 all on)
    const uint8_t cmd_inh[] = {kCmdChInhibits, 0xFF, 0xFF};
    if (sendCmdLocked("CH_INHIBITS", cmd_inh, 3, resp, rlen) != NV9DriverError::OK
            || !isRespOk(resp, rlen)) {
        qWarning() << "[NV9] CH_INHIBITS failed";
        return NV9DriverError::PROTOCOL_ERROR;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kInitDelayMs));

    // ENABLE
    const uint8_t cmd_en[] = {kCmdEnable};
    if (sendCmdLocked("ENABLE", cmd_en, 1, resp, rlen) != NV9DriverError::OK
            || !isRespOk(resp, rlen)) {
        qWarning() << "[NV9] ENABLE failed";
        return NV9DriverError::PROTOCOL_ERROR;
    }

    // Fallback VND channel table if device reported no channel values
    if (channel_value_[0] == 0) {
        memcpy(currency_, "VND", 4);
        channel_value_[0] =   1000;
        channel_value_[1] =   2000;
        channel_value_[2] =   5000;
        channel_value_[3] =  10000;
        channel_value_[4] =  20000;
        channel_value_[5] =  50000;
        channel_value_[6] = 100000;
        channel_value_[7] = 200000;
        channel_value_[8] = 500000;
        qInfo() << "[NV9] Using VND fallback channel table";
    }

    qInfo() << "[NV9] Ready — insert note";
    return NV9DriverError::OK;
}

// ── Poll event parser ─────────────────────────────────────────────────────────

/**
 * @brief Dispatch SSP POLL events.
 *
 * Updates state_ and invokes credit_cb_ / fault_cb_ for each event.
 * Callbacks are called without holding io_mutex_ so they may take
 * arbitrary time (Qt will queue the resulting signals).
 *
 * @return  0  normal
 *          1  EVT_SLAVE_RESET seen — re-run initSequence()
 *         -1  frame error (bad length or non-OK response code)
 */
int NV9ValidatorSSP::parsePoll(const uint8_t* resp, int len)
{
    if (len < 4 || resp[3] != kRespOk) return -1;

    int payload_len = resp[2];
    if (payload_len <= 1) return 0;  // response code only, no events

    int need_reinit = 0;

    for (int i = 4; i < 4 + payload_len - 1 && i < len - 2; i++) {
        uint8_t ev = resp[i];
        switch (ev) {
        case kEvtSlaveReset:
            qWarning() << "[NV9] Validator reset";
            rejected_fired_.store(false);
            need_reinit = 1;
            break;

        case kEvtNoteRead:
            state_ = NV9DriverState::Reading;
            rejected_fired_.store(false);
            qDebug() << "[NV9] Reading note";
            break;

        case kEvtNoteStacking:
            state_ = NV9DriverState::Stacking;
            qDebug() << "[NV9] Stacking";
            break;

        case kEvtNoteStacked:
            state_ = NV9DriverState::Stacked;
            qDebug() << "[NV9] Stacked";
            break;

        case kEvtNoteCredit: {
            if (i + 1 >= len - 2) break;
            uint8_t  ch  = resp[++i];
            uint32_t val = (ch >= 1 && ch <= (uint8_t)kMaxChannels)
                           ? channel_value_[ch - 1] : 0u;
            state_ = NV9DriverState::Enabled;
            rejected_fired_.store(false);
            qInfo().nospace() << "[NV9] Credit ch=" << ch
                              << " val=" << val << " " << currency_;
            if (credit_cb_)
                credit_cb_(ch, val, QString::fromLatin1(currency_));
            break;
        }

        case kEvtNoteRejected:
            state_ = NV9DriverState::Enabled;
            if (!rejected_fired_.exchange(true)) {
                qInfo() << "[NV9] Note rejected";
                if (fault_cb_) fault_cb_("Note rejected");
            }
            break;

        case kEvtNoteReturning:
            state_ = NV9DriverState::Rejecting;
            qDebug() << "[NV9] Note returning";
            break;

        case kEvtSafeJam:
            state_ = NV9DriverState::Jammed;
            qWarning() << "[NV9] Safe jam";
            if (fault_cb_) fault_cb_("Safe jam");
            break;

        case kEvtUnsafeJam:
            state_ = NV9DriverState::Jammed;
            qWarning() << "[NV9] Unsafe jam";
            if (fault_cb_) fault_cb_("Unsafe jam");
            break;

        case kEvtStackerFull:
            state_ = NV9DriverState::Fault;
            qWarning() << "[NV9] Stacker full";
            if (fault_cb_) fault_cb_("Stacker full");
            break;

        case kEvtDisabled:
            state_ = NV9DriverState::Disabled;
            break;

        case kEvtInitializing:
            qDebug() << "[NV9] Initializing";
            break;

        default:
            qDebug().nospace() << "[NV9] Unknown event 0x"
                               << Qt::hex << ev;
            break;
        }
    }
    return need_reinit;
}

// ── Public control API ────────────────────────────────────────────────────────

NV9DriverError NV9ValidatorSSP::enable()
{
    const uint8_t cmd[] = {kCmdEnable};
    uint8_t resp[kMaxFrameLen];
    int rlen = 0;
    auto err = sendCmd("ENABLE", cmd, 1, resp, rlen);
    if (err == NV9DriverError::OK && isRespOk(resp, rlen))
        state_ = NV9DriverState::Enabled;
    return err;
}

NV9DriverError NV9ValidatorSSP::disable()
{
    const uint8_t cmd[] = {kCmdDisable};
    uint8_t resp[kMaxFrameLen];
    int rlen = 0;
    auto err = sendCmd("DISABLE", cmd, 1, resp, rlen);
    if (err == NV9DriverError::OK && isRespOk(resp, rlen))
        state_ = NV9DriverState::Disabled;
    return err;
}

NV9DriverError NV9ValidatorSSP::rejectNote()
{
    const uint8_t cmd[] = {kCmdRejectNote};
    uint8_t resp[kMaxFrameLen];
    int rlen = 0;
    return sendCmd("REJECT", cmd, 1, resp, rlen);
}

NV9DriverError NV9ValidatorSSP::holdNote()
{
    const uint8_t cmd[] = {kCmdHold};
    uint8_t resp[kMaxFrameLen];
    int rlen = 0;
    return sendCmd("HOLD", cmd, 1, resp, rlen);
}

/**
 * @brief Send a hardware reset.
 *
 * The device will reboot and emit EVT_SLAVE_RESET on the next POLL.
 * The poll thread then automatically re-runs initSequence().
 */
NV9DriverError NV9ValidatorSSP::reset()
{
    const uint8_t cmd[] = {kCmdReset};
    uint8_t resp[kMaxFrameLen];
    int rlen = 0;
    auto err = sendCmd("RESET", cmd, 1, resp, rlen);
    if (err == NV9DriverError::OK)
        state_ = NV9DriverState::Idle;
    return err;
}

// ── Polling thread ────────────────────────────────────────────────────────────

void NV9ValidatorSSP::startPolling()
{
    if (polling_.exchange(true)) return;
    poll_thread_ = std::thread(&NV9ValidatorSSP::pollThreadFunc, this);
}

void NV9ValidatorSSP::stopPolling()
{
    if (!polling_.exchange(false)) return;
    if (poll_thread_.joinable()) poll_thread_.join();
}

/**
 * @brief Background poll loop with auto-reconnect.
 *
 * State machine:
 *   fd_ < 0   → openSerialPort() → on failure: sleep kReconnectDelayMs
 *   need_init → initSequence()   → on failure: closeSerialPort() + sleep
 *   (loop)    → sendCmd(POLL) every kPollIntervalMs
 *               EVT_SLAVE_RESET → set need_init, sleep kReenableDelayMs
 *               I/O error       → closeSerialPort() + sleep
 *
 * @thread_safety Serialises all serial I/O through io_mutex_.
 *                credit_cb_ / fault_cb_ invoked without holding io_mutex_.
 */
void NV9ValidatorSSP::pollThreadFunc()
{
    using namespace std::chrono;
    const uint8_t cmd_poll[] = {kCmdPoll};
    bool          need_init  = true;

    while (polling_.load()) {
        // ── (Re)connect serial port ───────────────────────────────────────
        if (fd_.load(std::memory_order_acquire) < 0) {
            qInfo() << "[NV9] Connecting to" << port_;
            if (openSerialPort() != NV9DriverError::OK) {
                if (fault_cb_) fault_cb_("Cannot open NV9 serial port");
                std::this_thread::sleep_for(milliseconds(kReconnectDelayMs));
                continue;
            }
            need_init = true;
        }

        // ── Init handshake after connect or slave reset ───────────────────
        if (need_init) {
            if (initSequence() != NV9DriverError::OK) {
                qWarning() << "[NV9] Init failed — reconnecting";
                if (fault_cb_) fault_cb_("NV9 init failed");
                closeSerialPort();
                std::this_thread::sleep_for(milliseconds(kReconnectDelayMs));
                continue;
            }
            state_    = NV9DriverState::Enabled;
            need_init = false;
        }

        // ── Poll ─────────────────────────────────────────────────────────
        uint8_t resp[kMaxFrameLen];
        int     rlen = 0;
        if (sendCmd("POLL", cmd_poll, 1, resp, rlen) != NV9DriverError::OK) {
            qWarning() << "[NV9] Lost connection";
            if (fault_cb_) fault_cb_("NV9 connection lost");
            closeSerialPort();
            std::this_thread::sleep_for(milliseconds(kReconnectDelayMs));
            continue;
        }

        int r = parsePoll(resp, rlen);
        if (r < 0) {
            qWarning() << "[NV9] Poll error — reconnecting";
            if (fault_cb_) fault_cb_("NV9 poll error");
            closeSerialPort();
            std::this_thread::sleep_for(milliseconds(kReconnectDelayMs));
            continue;
        }
        if (r == 1) {
            // EVT_SLAVE_RESET received — re-run init after settle
            need_init = true;
            std::this_thread::sleep_for(milliseconds(kReenableDelayMs));
            continue;
        }

        std::this_thread::sleep_for(milliseconds(kPollIntervalMs));
    }
}

#endif  // __APPLE__
