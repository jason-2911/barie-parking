/**
 * VehicleSensorGpiod.cpp
 * See header for overview. macOS: stub. Linux: libgpiod v1.
 */

#include "hardware/drivers/VehicleSensorGpiod.h"

#include <QDebug>

#include <utility>

#ifndef __APPLE__
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

#include <gpiod.h>
#endif

// ═════════════════════════════════════════════════════════════════════════════
// Shared bits (constructor / destructor / callback registration)
// ═════════════════════════════════════════════════════════════════════════════

VehicleSensorGpiod::VehicleSensorGpiod(Options opts)
    : opts_(std::move(opts)) {}

VehicleSensorGpiod::~VehicleSensorGpiod() { VehicleSensorGpiod::close(); }

void VehicleSensorGpiod::setStateCallback(StateCallback cb) {
    std::lock_guard<std::mutex> lock(cb_mutex_);
    cb_ = std::move(cb);
}

void VehicleSensorGpiod::deliver(bool present) {
    StateCallback cb;
    {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        cb = cb_;
    }
    if (cb) cb(present);
}

// ═════════════════════════════════════════════════════════════════════════════
// Platform-specific sections
// ═════════════════════════════════════════════════════════════════════════════

#ifdef __APPLE__

// ─── macOS STUB ─────────────────────────────────────────────────────────────

VehicleSensorError VehicleSensorGpiod::open() {
    qInfo().nospace() << "[VehicleSensor][STUB] open(chip="
                      << opts_.chip_path.c_str()
                      << ", line=" << opts_.line_offset
                      << ", active_low=" << opts_.active_low
                      << ", debounce_us=" << opts_.debounce_us << ")";
    running_.store(true, std::memory_order_release);
    return VehicleSensorError::Ok;
}

void VehicleSensorGpiod::close() {
    if (running_.exchange(false, std::memory_order_acq_rel)) {
        qInfo() << "[VehicleSensor][STUB] close()";
    }
    present_.store(false, std::memory_order_release);
}

bool VehicleSensorGpiod::isOpen() const {
    return running_.load(std::memory_order_acquire);
}

void VehicleSensorGpiod::runLoop() { /* never invoked on macOS */ }
void VehicleSensorGpiod::pollLive() { /* no hardware on macOS */ }

#else  // ── Linux (libgpiod v1) ────────────────────────────────────────────

#include <time.h>

namespace {

constexpr int kWakeRead  = 0;
constexpr int kWakeWrite = 1;

// Returns monotonic time in microseconds.
inline int64_t now_us()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000
         + static_cast<int64_t>(ts.tv_nsec) / 1000;
}

} // anonymous namespace

VehicleSensorError VehicleSensorGpiod::open()
{
    if (running_.load(std::memory_order_acquire))
        return VehicleSensorError::Ok;

    chip_ = gpiod_chip_open(opts_.chip_path.c_str());
    if (!chip_) {
        qCritical() << "[VehicleSensor] gpiod_chip_open("
                    << opts_.chip_path.c_str() << ") failed:" << strerror(errno);
        return VehicleSensorError::IoError;
    }

    line_ = gpiod_chip_get_line(chip_, opts_.line_offset);
    if (!line_) {
        qCritical() << "[VehicleSensor] gpiod_chip_get_line("
                    << opts_.line_offset << ") failed:" << strerror(errno);
        gpiod_chip_close(chip_);
        chip_ = nullptr;
        return VehicleSensorError::IoError;
    }

    // Build request flags (v1 uses a bitmask, not separate setter calls).
    int flags = 0;
    if (opts_.active_low)   flags |= GPIOD_LINE_REQUEST_FLAG_ACTIVE_LOW;
#ifdef GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP
    if (opts_.bias_pull_up) flags |= GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP;
    else                    flags |= GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE;
#endif

    struct gpiod_line_request_config req_cfg;
    req_cfg.consumer     = opts_.consumer_label.c_str();
    req_cfg.request_type = GPIOD_LINE_REQUEST_EVENT_BOTH_EDGES;
    req_cfg.flags        = flags;

    if (gpiod_line_request(line_, &req_cfg, 0) != 0) {
        qCritical() << "[VehicleSensor] gpiod_line_request failed:"
                    << strerror(errno);
        gpiod_chip_close(chip_);
        line_ = nullptr;
        chip_ = nullptr;
        return VehicleSensorError::IoError;
    }

    // Do NOT seed present_ from the startup GPIO read. When the barrier arm is
    // in the DOWN position it physically blocks the sensor beam — the NPN
    // output pulls LOW, active_low logic inverts it to logical-HIGH, and
    // gpiod_line_get_value() returns 1 ("present") even with no vehicle. This
    // causes every barrier open to immediately skip to Passing state.
    // Start as NOT present; BarrierManager calls pollLive() after 2 s
    // (once the arm has cleared) to pick up any vehicle that was already
    // blocking the beam before the gate opened.
    const int init_val = gpiod_line_get_value(line_);
    if (init_val > 0) {
        qWarning() << "[VehicleSensor] startup read shows PRESENT — ignoring"
                      " (barrier arm may be blocking beam). pollLive() will"
                      " re-read once the arm has cleared.";
    }
    present_.store(false, std::memory_order_release);

    if (pipe2(wake_fd_, O_NONBLOCK | O_CLOEXEC) != 0) {
        qCritical() << "[VehicleSensor] pipe2 failed:" << strerror(errno);
        gpiod_line_release(line_);
        gpiod_chip_close(chip_);
        line_ = nullptr;
        chip_ = nullptr;
        return VehicleSensorError::IoError;
    }

    running_.store(true, std::memory_order_release);
    worker_ = std::thread(&VehicleSensorGpiod::runLoop, this);

    qInfo().nospace() << "[VehicleSensor] open chip=" << opts_.chip_path.c_str()
                      << " line=" << opts_.line_offset
                      << " active_low=" << opts_.active_low
                      << " pullup=" << opts_.bias_pull_up
                      << " debounce_us=" << opts_.debounce_us
                      << " initial_present=" << present_.load();
    return VehicleSensorError::Ok;
}

void VehicleSensorGpiod::close()
{
    if (!running_.exchange(false, std::memory_order_acq_rel))
        return;

    // Wake the worker out of poll().
    if (wake_fd_[kWakeWrite] >= 0) {
        const char b = 1;
        ssize_t n = ::write(wake_fd_[kWakeWrite], &b, 1);
        (void)n;
    }
    if (worker_.joinable()) worker_.join();

    if (wake_fd_[kWakeRead]  >= 0) { ::close(wake_fd_[kWakeRead]);  wake_fd_[kWakeRead]  = -1; }
    if (wake_fd_[kWakeWrite] >= 0) { ::close(wake_fd_[kWakeWrite]); wake_fd_[kWakeWrite] = -1; }

    if (line_) { gpiod_line_release(line_); line_ = nullptr; }
    if (chip_) { gpiod_chip_close(chip_);   chip_ = nullptr; }

    present_.store(false, std::memory_order_release);
    qInfo() << "[VehicleSensor] closed";
}

bool VehicleSensorGpiod::isOpen() const
{
    return running_.load(std::memory_order_acquire);
}

void VehicleSensorGpiod::pollLive()
{
    // Called from the GUI thread after a startup delay to catch a vehicle
    // that was already blocking the beam when the gate opened (no edge event
    // fires in that case). Safe to call from main thread: GPIOHANDLE ioctl is
    // independent from the event fd used by the worker thread.
    if (!running_.load(std::memory_order_acquire) || !line_) return;
    const int val = gpiod_line_get_value(line_);
    if (val < 0) {
        qWarning() << "[VehicleSensor] pollLive: gpiod_line_get_value failed:"
                   << strerror(errno);
        return;
    }
    const bool present = (val > 0);
    const bool prev    = present_.exchange(present, std::memory_order_acq_rel);
    if (prev != present) {
        qInfo() << "[VehicleSensor] pollLive: state changed to"
                << (present ? "PRESENT" : "CLEAR");
        deliver(present);
    }
}

void VehicleSensorGpiod::runLoop()
{
    // v1 gives a regular pollable fd for the line's event queue.
    const int line_fd = gpiod_line_event_get_fd(line_);
    const int wake_fd = wake_fd_[kWakeRead];

    struct pollfd fds[2];
    fds[0].fd = line_fd; fds[0].events = POLLIN;
    fds[1].fd = wake_fd; fds[1].events = POLLIN;

    // Software debounce: track when the last accepted edge occurred.
    int64_t last_edge_us = 0;

    while (running_.load(std::memory_order_acquire)) {
        fds[0].revents = 0;
        fds[1].revents = 0;

        const int rc = ::poll(fds, 2, -1);
        if (rc < 0) {
            if (errno == EINTR) continue;
            qWarning() << "[VehicleSensor] poll failed:" << strerror(errno);
            break;
        }
        if (fds[1].revents & POLLIN) break;  // shutdown pipe
        if (!(fds[0].revents & POLLIN)) continue;

        struct gpiod_line_event ev;
        if (gpiod_line_event_read(line_, &ev) != 0) {
            qWarning() << "[VehicleSensor] event_read failed:" << strerror(errno);
            continue;
        }

        // Software debounce: discard edges that arrive within debounce_us of the last one.
        const int64_t now = now_us();
        if ((now - last_edge_us) < static_cast<int64_t>(opts_.debounce_us))
            continue;
        last_edge_us = now;

        const bool present = (ev.event_type == GPIOD_LINE_EVENT_RISING_EDGE);
        const bool prev    = present_.exchange(present, std::memory_order_acq_rel);
        if (prev != present) deliver(present);
    }
}

#endif
