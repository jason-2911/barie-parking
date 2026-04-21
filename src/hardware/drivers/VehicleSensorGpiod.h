/**
 * VehicleSensorGpiod.h
 * E3F-DS10C4 IR proximity sensor driver.
 *
 * Linux:  libgpiod v1 implementation. Opens one line on a gpiochip,
 *         arms edge-triggered events, and runs a dedicated worker thread
 *         that blocks in poll() on the line fd. Software debounce applied
 *         in the worker loop (v1 has no kernel-level debounce API).
 *         Thread-safe.
 *
 * macOS:  compile-only stub that logs calls and never reports presence,
 *         so the rest of the application can be developed on a laptop.
 */

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "hardware/IVehicleSensor.h"

#ifndef __APPLE__
struct gpiod_chip;
struct gpiod_line;
#endif

class VehicleSensorGpiod final : public IVehicleSensor {
public:
    struct Options {
        std::string chip_path       = "/dev/gpiochip0";
        uint32_t    line_offset     = 17;   // BCM pin number on the chip
        bool        active_low      = true; // NPN-NO sinks when triggered
        bool        bias_pull_up    = true; // enable internal pull-up
        uint32_t    debounce_us     = 20000;// 20 ms hardware debounce
        std::string consumer_label  = "parking-vehicle-sensor";
    };

    explicit VehicleSensorGpiod(Options opts);
    ~VehicleSensorGpiod() override;

    VehicleSensorGpiod(const VehicleSensorGpiod&)            = delete;
    VehicleSensorGpiod& operator=(const VehicleSensorGpiod&) = delete;

    [[nodiscard]] VehicleSensorError open() override;
    void close() override;
    [[nodiscard]] bool isOpen() const override;

    [[nodiscard]] bool isPresent() const override {
        return present_.load(std::memory_order_acquire);
    }

    void setStateCallback(StateCallback cb) override;

private:
    void runLoop();                         // worker-thread entry
    void deliver(bool present);             // thread-safe dispatch

    Options                opts_;
    std::atomic<bool>      present_{false};
    std::atomic<bool>      running_{false};
    std::thread            worker_;

    mutable std::mutex     cb_mutex_;
    StateCallback          cb_;

#ifndef __APPLE__
    gpiod_chip*   chip_    = nullptr;
    gpiod_line*   line_    = nullptr;
    int           wake_fd_[2] = {-1, -1}; // pipe for clean shutdown
#endif
};
