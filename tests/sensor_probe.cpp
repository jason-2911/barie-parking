/**
 * @file sensor_probe.cpp
 * @brief Hardware bring-up probe for the vehicle-presence sensor.
 *
 * Opens the same VehicleSensorGpiod driver the production app uses, prints
 * the initial state, and then prints "1" / "0" on every debounced edge —
 * so you can wave a hand in front of the sensor and watch the line flip
 * live. Heartbeat line every 2 s shows the current state in case there
 * are no edges (e.g. sensor wired wrong / always present / always idle).
 *
 * Usage:
 *   ./sensor_probe                           # defaults: /dev/gpiochip0, line 17,
 *                                            # active-low, pull-up, 20 ms debounce
 *   ./sensor_probe --chip /dev/gpiochip1 --line 23
 *   ./sensor_probe --no-active-low --no-pull-up --debounce-us 5000
 *
 * Exit cleanly with Ctrl-C — the worker thread is joined and the gpio
 * line is released.
 *
 * macOS: builds against the stubbed driver. It will print the initial
 * state once and then sit idle (no edges) — there's no real GPIO. Useful
 * only as a compile check; do the actual wiring test on the Orange Pi.
 */

#include "hardware/drivers/VehicleSensorGpiod.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_running{true};

void onSignal(int /*sig*/) { g_running.store(false); }

const char* nowStamp() {
    using namespace std::chrono;
    static thread_local char buf[32];
    auto now = system_clock::now();
    auto ms  = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::snprintf(buf, sizeof(buf),
                   "%02d:%02d:%02d.%03lld",
                   tm.tm_hour, tm.tm_min, tm.tm_sec,
                   static_cast<long long>(ms.count()));
    return buf;
}

void printUsage(const char* argv0) {
    std::fprintf(stderr,
        "usage: %s [options]\n"
        "  --chip PATH         gpiochip device (default /dev/gpiochip0)\n"
        "  --line N            line offset on the chip (default 17)\n"
        "  --active-low        invert: LOW = present  (default ON for NPN-NO)\n"
        "  --no-active-low     HIGH = present\n"
        "  --pull-up           enable internal pull-up (default ON)\n"
        "  --no-pull-up        disable internal pull-up\n"
        "  --debounce-us US    software debounce window (default 20000)\n"
        "  -h, --help          this help\n",
        argv0);
}

bool parseArgs(int argc, char* argv[], VehicleSensorGpiod::Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto take = [&](const char* /*name*/) -> const char* {
            if (i + 1 >= argc) return nullptr;
            return argv[++i];
        };
        if      (a == "--chip")          { auto v = take("--chip");
                                            if (!v) return false; opts.chip_path = v; }
        else if (a == "--line")          { auto v = take("--line");
                                            if (!v) return false; opts.line_offset = std::atoi(v); }
        else if (a == "--active-low")    opts.active_low   = true;
        else if (a == "--no-active-low") opts.active_low   = false;
        else if (a == "--pull-up")       opts.bias_pull_up = true;
        else if (a == "--no-pull-up")    opts.bias_pull_up = false;
        else if (a == "--debounce-us")   { auto v = take("--debounce-us");
                                            if (!v) return false; opts.debounce_us = std::atoi(v); }
        else if (a == "-h" || a == "--help") { printUsage(argv[0]); std::exit(0); }
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return false; }
    }
    return true;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    VehicleSensorGpiod::Options opts;     // defaults match config.json
    if (!parseArgs(argc, argv, opts)) {
        printUsage(argv[0]);
        return 2;
    }

    std::signal(SIGINT,  onSignal);
    std::signal(SIGTERM, onSignal);

    std::printf("=== Vehicle Sensor Probe ===\n");
    std::printf("chip          : %s\n",  opts.chip_path.c_str());
    std::printf("line offset   : %u\n",  opts.line_offset);
    std::printf("active-low    : %s\n",  opts.active_low   ? "yes" : "no");
    std::printf("internal pull : %s\n",  opts.bias_pull_up ? "up"  : "off");
    std::printf("debounce      : %u µs\n", opts.debounce_us);
    std::printf("Press Ctrl-C to stop. Waving a hand in front of the sensor\n"
                "should toggle the value between 0 and 1.\n\n");

    VehicleSensorGpiod sensor(opts);
    if (sensor.open() != VehicleSensorError::Ok) {
        std::fprintf(stderr, "open() failed — check that the user has "
                              "access to %s and the line is free\n",
                              opts.chip_path.c_str());
        return 1;
    }

    std::atomic<int> edges{0};
    sensor.setStateCallback([&](bool present) {
        ++edges;
        std::printf("[%s] EDGE  -> %d  (%s)\n",
                     nowStamp(),
                     present ? 1 : 0,
                     present ? "vehicle/object PRESENT"
                              : "cleared");
        std::fflush(stdout);
    });

    std::printf("[%s] init  =  %d  (initial reading)\n",
                 nowStamp(), sensor.isPresent() ? 1 : 0);

    // Heartbeat: every 2 s reprint current state and edge count so the
    // operator sees something even when the line never toggles.
    auto last_beat = std::chrono::steady_clock::now();
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto now = std::chrono::steady_clock::now();
        if (now - last_beat >= std::chrono::seconds(2)) {
            std::printf("[%s] beat  =  %d   edges_seen=%d\n",
                         nowStamp(),
                         sensor.isPresent() ? 1 : 0,
                         edges.load());
            std::fflush(stdout);
            last_beat = now;
        }
    }

    std::printf("\n[%s] stopping — total edges seen: %d\n",
                 nowStamp(), edges.load());
    sensor.close();
    return 0;
}
