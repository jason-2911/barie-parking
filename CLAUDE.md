# CLAUDE.md — Production C/C++ Coding Standards

This document defines the coding standards, conventions, and practices required
for all C and C++ code in this project. Claude must follow these rules strictly
when writing, reviewing, or modifying any code.

References:
- [Linux Kernel Coding Style](https://www.kernel.org/doc/html/latest/process/coding-style.html)
- [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
- [LLVM Coding Standards](https://llvm.org/docs/CodingStandards.html)
- [SEI CERT C Coding Standard](https://wiki.sei.cmu.edu/confluence/display/c)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [MISRA C:2012](https://misra.org.uk/) (safety-critical subset)

---

## 1. Language & Standard

```cmake
set(CMAKE_CXX_STANDARD 17)     # C++17 minimum
set(CMAKE_C_STANDARD   11)     # C11 minimum
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD_REQUIRED   ON)
```

- **C**: Use C11 (`_Static_assert`, `_Alignas`, atomics via `<stdatomic.h>`)
- **C++**: Use C++17 (`std::optional`, `std::string_view`, structured bindings, `if constexpr`)
- **No**: C++20 modules, C++23 features (cross-platform compat)
- **No**: GNU extensions unless explicitly needed (`__attribute__` is OK when guarded)

---

## 2. Compiler Flags (Required)

### Release

```cmake
target_compile_options(target PRIVATE
    -Wall
    -Wextra
    -Werror                      # All warnings are errors in CI
    -Wpedantic
    -Wformat=2
    -Wformat-overflow=2
    -Wformat-truncation=2
    -Wshadow
    -Wwrite-strings
    -Wcast-align
    -Wpointer-arith
    -Wstrict-prototypes          # C only
    -Wmissing-prototypes         # C only
    -Wold-style-definition       # C only
    -Wredundant-decls
    -Wnull-dereference
    -Wdouble-promotion
    -Wundef
    -fno-common
    -O2
    -D_FORTIFY_SOURCE=2
    -fstack-protector-strong
    -fstack-clash-protection
    -fcf-protection              # x86 only
    -fPIE
)
target_link_options(target PRIVATE -pie -Wl,-z,relro,-z,now)
```

### Debug / Sanitizers

```cmake
target_compile_options(target PRIVATE
    -g3 -Og
    -fsanitize=address,undefined,leak
    -fno-omit-frame-pointer
)
target_link_options(target PRIVATE -fsanitize=address,undefined,leak)
```

### Static Analysis (run in CI)

```bash
clang-tidy --checks='*,-fuchsia-*,-llvmlibc-*' src/**/*.cpp
cppcheck --enable=all --error-exitcode=1 --std=c++17 src/
scan-build cmake --build build/
```

---

## 3. Naming Conventions

### C

```c
/* Types: snake_case_t */
typedef struct barrier_ctx   barrier_ctx_t;
typedef uint32_t             process_id_t;
typedef enum fault_code      fault_code_t;

/* Functions: module_verb_noun */
int  barrier_open(barrier_ctx_t *ctx, uint8_t addr);
void barrier_close(barrier_ctx_t *ctx);
bool barrier_is_online(const barrier_ctx_t *ctx);

/* Macros/Constants: UPPER_SNAKE */
#define BARRIER_MAX_SLAVES   32
#define MODBUS_TIMEOUT_MS    500
#define IS_VALID_ADDR(a)     ((a) >= 1 && (a) <= 247)

/* Variables: snake_case */
int    retry_count = 0;
char  *device_path = NULL;
size_t bytes_read  = 0;
```

### C++

```cpp
// Classes: PascalCase
class BarrierController { ... };
class MqttClient { ... };

// Methods: camelCase
void openBarrier(uint8_t addr);
bool isConnected() const;
BarrierStatus readStatus(uint8_t addr);

// Members: snake_case_ (trailing underscore)
int    retry_count_   = 0;
bool   is_open_       = false;
std::string port_name_;

// Enums: PascalCase type, PascalCase values
enum class BarrierError { Ok, Timeout, CrcMismatch, NotFound };
enum class Direction    { Entry, Exit, Unknown };

// Constants: kPascalCase (Google) or UPPER_SNAKE
constexpr int kMaxSlaves    = 32;
constexpr int kTimeoutMs    = 500;

// Namespaces: snake_case
namespace parking { namespace mqtt { ... } }

// Template parameters: PascalCase
template<typename PayloadType, size_t MaxSize>
```

---

## 4. File Structure & Layout

### Header file (.h / .hpp)

```cpp
/**
 * @file barrier_controller.hpp
 * @brief Modbus RTU RS485 barrier gate controller
 *
 * Controls one or more BLDC barrier gates over a shared RS485 bus.
 * Thread-safe: all public methods acquire the bus mutex.
 *
 * @author  Your Name
 * @date    2026-04-20
 * SPDX-License-Identifier: MIT
 */

#pragma once                    // Always use #pragma once, never include guards

// 1. Standard library headers (alphabetical)
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

// 2. Third-party headers
#include <QObject>

// 3. Project headers
#include "types/MessageTypes.h"

// Forward declarations before class definition
class SerialPort;

namespace parking {

// Constants belong in the header only if they are part of the API
constexpr uint8_t kModbusMaxSlaves = 32;

class BarrierController {
public:
    // --- Constructors / Destructor ---
    explicit BarrierController(std::string port, int baud = 9600);
    ~BarrierController();

    // Rule of Five: explicitly define or delete
    BarrierController(const BarrierController&)            = delete;
    BarrierController& operator=(const BarrierController&) = delete;
    BarrierController(BarrierController&&)                 = default;
    BarrierController& operator=(BarrierController&&)      = default;

    // --- Public API (grouped logically) ---

    // Lifecycle
    [[nodiscard]] BarrierError open();
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return fd_ >= 0; }

    // Discovery
    std::vector<uint8_t> scan(uint8_t start = 1, uint8_t end = 32);

    // Control
    [[nodiscard]] BarrierError up(uint8_t addr);
    [[nodiscard]] BarrierError down(uint8_t addr);

private:
    // Private methods
    [[nodiscard]] BarrierError sendCoil(uint8_t slave, uint16_t coil, bool on);
    static uint16_t crc16(const uint8_t* data, size_t len) noexcept;

    // Data members (trailing underscore)
    std::string        port_;
    int                baud_;
    int                fd_     = -1;
    mutable std::mutex mutex_;      // mutable: allows locking in const methods
};

} // namespace parking
```

### Source file (.cpp)

```cpp
/**
 * @file barrier_controller.cpp
 * @brief See barrier_controller.hpp
 */

// 1. Matching header FIRST
#include "hardware/barrier_controller.hpp"

// 2. Standard library (alphabetical)
#include <cerrno>
#include <cstring>

// 3. System headers
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

// 4. Third-party
#include <spdlog/spdlog.h>

// 5. Project headers
#include "app/Config.h"

namespace parking {

// File-local constants (not in header — implementation detail)
namespace {
constexpr int kInterCmdDelayMs = 50;
constexpr int kReadTimeoutMs   = 300;
} // anonymous namespace

// Implementations follow declaration order from header
BarrierController::BarrierController(std::string port, int baud)
    : port_(std::move(port))   // prefer std::move for string args
    , baud_(baud)
    , fd_(-1)
{}

} // namespace parking
```

---

## 5. Memory Management

### C — Ownership is explicit

```c
/* RULE: Every malloc() has exactly one matching free().
 * Use goto-cleanup pattern for early returns. */

int process_data(const char *input, size_t len) {
    char *buf     = NULL;
    uint8_t *work = NULL;
    int ret       = 0;

    if (!input || len == 0 || len > MAX_INPUT_LEN) {
        return -EINVAL;
    }

    buf = malloc(len + 1);
    if (!buf) { ret = -ENOMEM; goto out; }

    work = malloc(len * 2);
    if (!work) { ret = -ENOMEM; goto out; }

    ret = do_work(buf, work, len);

out:
    free(work);  // free(NULL) is always safe
    free(buf);
    return ret;
}
```

### C++ — RAII, no naked new/delete

```cpp
// GOOD: RAII everywhere
std::unique_ptr<BarrierController> ctrl =
    std::make_unique<BarrierController>("/dev/ttyUSB0");

std::vector<uint8_t> buffer(1024);   // auto-managed
std::string          msg = "hello";  // auto-managed

// GOOD: shared ownership
auto shared_cfg = std::make_shared<Config>();

// BAD: naked owning raw pointer
BarrierController* ctrl = new BarrierController(...);   // NEVER

// GOOD: non-owning raw pointer is OK
void process(const Config* cfg);   // observing pointer, no ownership

// GOOD: prefer references over pointers where possible
void process(const Config& cfg);   // even better

// Containers: use standard library
std::array<uint8_t, 8>     fixed;
std::vector<uint8_t>       dynamic;
std::unordered_map<int,int> lookup;
```

---

## 6. Error Handling

### C — errno-style return codes

```c
/* Return 0 on success, -errno on failure.
 * Never return negative errno as positive. */

#include <errno.h>

int barrier_open(barrier_ctx_t *ctx) {
    if (!ctx) return -EINVAL;

    ctx->fd = open(ctx->port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (ctx->fd < 0) {
        int saved = errno;
        log_error("open(%s) failed: %s", ctx->port, strerror(saved));
        return -saved;
    }
    return 0;
}

/* Caller pattern */
int ret = barrier_open(&ctx);
if (ret < 0) {
    log_error("barrier_open: %s", strerror(-ret));
    return ret;
}
```

### C++ — std::expected (C++23) or typed enum errors

```cpp
// Use typed enum errors — never throw in embedded/system code
enum class BarrierError : int {
    Ok              = 0,
    InvalidArg      = -EINVAL,
    IoError         = -EIO,
    Timeout         = -ETIMEDOUT,
    CrcMismatch     = -EBADMSG,
    DeviceFault     = -EFAULT,
    NotOpen         = -EBADF,
};

// [[nodiscard]] forces callers to check return value
[[nodiscard]] BarrierError BarrierController::up(uint8_t addr) {
    if (!isOpen())    return BarrierError::NotOpen;
    if (addr == 0)    return BarrierError::InvalidArg;

    auto err = sendCoil(addr, kCoilUp, true);
    if (err != BarrierError::Ok) {
        spdlog::error("[Barrier] up(addr={}) failed: {}", addr, toString(err));
        return err;
    }
    return BarrierError::Ok;
}

// Caller MUST check:
if (auto err = ctrl.up(1); err != BarrierError::Ok) {
    handleError(err);
}

// NEVER: use exceptions in embedded / real-time code
// NEVER: ignore [[nodiscard]] return values
```

---

## 7. Null & Bounds Safety

```c
/* C: validate ALL pointer arguments */
int barrier_read_status(barrier_ctx_t *ctx, barrier_status_t *out) {
    /* Document preconditions, then enforce them */
    assert(ctx != NULL);   // programmer error: assert
    if (!out) return -EINVAL;  // caller error: return code

    if (ctx->fd < 0) return -EBADF;
    ...
}

/* Array access: always bounds-check external indices */
if (channel >= MAX_CHANNELS) {
    log_error("channel %u out of range [0,%u)", channel, MAX_CHANNELS);
    return -ERANGE;
}
value = channel_table[channel];  // safe

/* String operations: never use strcpy/sprintf/gets */
// BAD:
strcpy(dst, src);          // NEVER — buffer overflow
sprintf(buf, fmt, val);    // NEVER

// GOOD:
strlcpy(dst, src, dst_size);
snprintf(buf, sizeof(buf), "%s", src);
```

```cpp
/* C++: use .at() for checked access, or assert bounds explicitly */
auto& val = vec.at(idx);          // throws std::out_of_range
auto& val = vec[idx];             // only after bounds check

// Use std::span for array views (C++20) or gsl::span
void process(std::span<const uint8_t> data) {
    // data.size() is always valid, no raw pointer arithmetic
}
```

---

## 8. Concurrency & Thread Safety

```cpp
/* Document thread safety in class header:
 * Thread-safe:     All public methods acquire mutex_
 * Not thread-safe: External synchronization required
 * Immutable:       Safe to share without locking (const after ctor)
 */

class BarrierController {
public:
    // Thread-safe: acquires bus_mutex_ before every operation
    [[nodiscard]] BarrierError up(uint8_t addr);

private:
    mutable std::mutex bus_mutex_;  // guards all serial I/O
};

BarrierError BarrierController::up(uint8_t addr) {
    std::lock_guard<std::mutex> lock(bus_mutex_);
    return sendCoilLocked(addr, kCoilUp, true);   // _Locked suffix = must hold mutex
}

/* Atomics for simple flags — no mutex needed */
std::atomic<bool>  running_{false};
std::atomic<NV9State> state_{NV9State::Idle};

running_.store(true, std::memory_order_release);
if (running_.load(std::memory_order_acquire)) { ... }

/* NEVER: data race on shared variable without protection */
// BAD:
bool running = false;   // shared between threads — UB
// GOOD:
std::atomic<bool> running{false};

/* Prefer std::jthread (C++20) or RAII thread wrapper */
std::jthread poll_thread_([this](std::stop_token st) {
    while (!st.stop_requested()) {
        pollOnce();
        std::this_thread::sleep_for(200ms);
    }
});
```

---

## 9. Integer & Arithmetic Safety

```c
/* Use fixed-width types from <stdint.h> for all protocol/hardware code */
uint8_t  slave_addr;      // 0-255
uint16_t register_value;  // Modbus register
uint32_t total_runs;      // 32-bit counter
int32_t  error_code;      // signed error

/* Check overflow before arithmetic */
uint32_t a = get_value();
uint32_t b = get_delta();
if (b > UINT32_MAX - a) {
    return -EOVERFLOW;
}
uint32_t sum = a + b;

/* Cast explicitly, never silently truncate */
// BAD:
int len = read(fd, buf, BIG_SIZE);   // ssize_t → int, may truncate
// GOOD:
ssize_t n = read(fd, buf, BIG_SIZE);
if (n < 0 || (size_t)n > sizeof(buf)) { ... }

/* No implicit conversions between signed/unsigned */
size_t   len  = strlen(s);       // unsigned
int      diff = (int)len - 5;    // explicit cast, OK
if (diff < 0) { ... }            // safe comparison
```

---

## 10. const Correctness

```cpp
/* const everything that should not change */

// Method that does not modify state: const
[[nodiscard]] bool isOpen()          const noexcept;
[[nodiscard]] std::string portName() const noexcept { return port_; }

// Parameter: const ref for read-only input
void setConfig(const Config& cfg);
void process(const std::vector<uint8_t>& data);

// Return const ref to avoid copy, prevent mutation
const std::vector<uint8_t>& slaves() const { return slaves_; }

// Pointer semantics: be explicit
void read(const uint8_t* src, size_t len);  // src is read-only
void write(uint8_t* dst, size_t len);       // dst is written

// C: const on all pointer params that are not modified
int crc_compute(const uint8_t *data, size_t len);
```

---

## 11. Functions & API Design

```cpp
/* Single Responsibility: one function does one thing */
// BAD: parse + validate + send + log all in one function
// GOOD: split into parsePacket(), validateCrc(), sendRaw(), logPacket()

/* Function length: max ~50 lines. If longer, extract helpers. */

/* Parameters:
 * 0-2 params: fine as values or refs
 * 3+  params: consider a config struct */

// BAD:
BarrierError open(const char* port, int baud, int timeout,
                   bool rts, int parity, int stop_bits);

// GOOD:
struct SerialConfig {
    std::string port;
    int         baud    = 9600;
    int         timeout = 500;
    bool        rts     = false;
};
BarrierError open(const SerialConfig& cfg);

/* Output parameters: prefer return value over out-pointer */
// BAD:
void getStatus(BarrierStatus* out);   // nullable? who owns?
// GOOD:
[[nodiscard]] std::optional<BarrierStatus> readStatus();

/* Early return to reduce nesting */
// BAD:
BarrierError doThing() {
    if (isOpen()) {
        if (addr > 0) {
            if (mutex_.try_lock()) {
                // ... deeply nested
            }
        }
    }
    return BarrierError::Ok;
}
// GOOD:
BarrierError doThing() {
    if (!isOpen())         return BarrierError::NotOpen;
    if (addr == 0)         return BarrierError::InvalidArg;
    if (!mutex_.try_lock()) return BarrierError::Busy;
    // ... flat body
    return BarrierError::Ok;
}
```

---

## 12. Comments & Documentation

```cpp
/**
 * @brief Send a Modbus FC05 coil write command.
 *
 * Builds the 8-byte Modbus RTU frame, writes it to the serial port,
 * and reads back the echo response. The barrier echoes the request
 * verbatim on success.
 *
 * @param slave  Slave address (1–247). Must match H00-31 on device.
 * @param coil   Coil address: 0x0000=up, 0x0001=down, 0x0003=self-test
 * @param on     true = set (0xFF00), false = clear (0x0000)
 * @return       BarrierError::Ok on success, error code otherwise.
 *
 * @pre  isOpen() == true
 * @pre  Caller holds bus_mutex_
 * @thread_safety Not thread-safe — caller must hold bus_mutex_
 */
[[nodiscard]] BarrierError sendCoilLocked(uint8_t slave,
                                           uint16_t coil,
                                           bool on);

/* Inline: explain WHY, not WHAT */

// BAD: obvious comment
i++;   // increment i

// GOOD: explain non-obvious reasoning
seq_bit_ ^= 1;   // SSP spec §4.2: alternate seq bit each command
                 // to detect duplicate/lost messages

// TODO/FIXME must include owner and issue number
// TODO(jason): replace polling with interrupt-driven read — #42
// FIXME(jason): CRC check skipped for broadcast addr 0 — #57
```

---

## 13. Logging

```cpp
/* Use structured logging with levels. Project uses spdlog. */
#include <spdlog/spdlog.h>

// Format: [module] message — key=value pairs
spdlog::trace("[Barrier] TX: {:02X} {:02X} {:02X}", buf[0], buf[1], buf[2]);
spdlog::debug("[Barrier] addr={} coil=0x{:04X}", addr, coil);
spdlog::info ("[Barrier] Found barrier @ addr={}", addr);
spdlog::warn ("[Barrier] addr={} retrying ({}/3)", addr, attempt);
spdlog::error("[Barrier] CRC mismatch: expected={:04X} got={:04X}", exp, got);
spdlog::critical("[Barrier] Serial port lost — attempting reopen");

/* Log levels by build type */
// Debug build:   TRACE and above
// Release build: INFO and above
// Production:    WARN and above (configurable at runtime)

/* NEVER log sensitive data */
// BAD:
spdlog::info("MQTT password={}", cfg.password);
// GOOD:
spdlog::info("MQTT connecting user={}", cfg.username);

/* Hex dump helper: only in TRACE / debug mode */
#ifdef NDEBUG
#define LOG_HEX(tag, buf, len)  (void)0
#else
#define LOG_HEX(tag, buf, len) log_hex_dump(tag, buf, len)
#endif
```

---

## 14. Testing

### Unit Tests (Catch2 or Google Test)

```cpp
#include <catch2/catch_all.hpp>

TEST_CASE("CRC16 Modbus", "[modbus][crc]") {
    // Reference vectors from Modbus spec
    SECTION("known vector 1") {
        uint8_t data[] = {0x01, 0x03, 0xD0, 0x14, 0x00, 0x01};
        REQUIRE(crc16_modbus(data, 6) == 0xCEFC);
    }
    SECTION("empty input") {
        REQUIRE(crc16_modbus(nullptr, 0) == 0xFFFF);
    }
    SECTION("single byte") {
        uint8_t b = 0x01;
        REQUIRE(crc16_modbus(&b, 1) == 0x807E);
    }
}

TEST_CASE("BarrierController scan", "[barrier][integration]") {
    // Use mock serial port for unit test
    MockSerial serial;
    serial.setResponse({0x01, 0x03, 0x02, 0x00, 0x00, 0xFD, 0x84});  // valid response from addr 1

    BarrierController ctrl(serial);
    REQUIRE(ctrl.open() == BarrierError::Ok);

    auto found = ctrl.scan(1, 3);
    REQUIRE(found.size() == 1);
    REQUIRE(found[0] == 1);
}
```

### Coverage requirement

```bash
# Minimum 80% line coverage on library code (non-UI, non-main)
gcov --branch-probabilities --all-blocks
lcov --capture --directory . --output-file coverage.info
lcov --remove coverage.info '/usr/*' '*/test/*' --output-file coverage.info
genhtml coverage.info --output-directory coverage_report
```

---

## 15. Code Review Checklist

Before every PR/commit, verify:

**Correctness**
- [ ] No uninitialized variables
- [ ] All return values of `[[nodiscard]]` functions checked
- [ ] All `malloc`/`new` results checked for null
- [ ] No buffer overflows (use `snprintf`, `strlcpy`, `.at()`)
- [ ] No use-after-free, double-free
- [ ] Integer overflow checks on untrusted input

**Concurrency**
- [ ] Shared data protected by mutex or atomic
- [ ] No deadlocks (lock ordering documented)
- [ ] No `static` local variables in multithreaded code (unless `const`)

**API**
- [ ] All public functions documented with `@brief`, `@param`, `@return`
- [ ] Error paths return meaningful error codes
- [ ] No breaking changes to existing public API without version bump

**Style**
- [ ] Naming follows conventions (§3)
- [ ] No lines > 100 chars
- [ ] `clang-format` applied (`clang-format -i src/**/*.{cpp,h}`)
- [ ] No commented-out dead code committed
- [ ] No debug `printf` / `std::cout` left in

**Security**
- [ ] No `system()`, `popen()`, `exec*()` with unsanitized input
- [ ] No format string vulnerabilities (`printf(user_str)` → NEVER)
- [ ] Sensitive data cleared after use (`explicit_bzero` / `memset_s`)

---

## 16. clang-format Config

`.clang-format` in repo root:

```yaml
BasedOnStyle:           Google
IndentWidth:            4
ColumnLimit:            100
AccessModifierOffset:   -4
AlignConsecutiveAssignments:
  Enabled:              true
  AcrossEmptyLines:     false
  AcrossComments:       false
AlignConsecutiveDeclarations:
  Enabled:              false
AlignTrailingComments:
  Kind:                 Always
  OverEmptyLines:       1
BraceWrapping:
  AfterFunction:        true
  AfterClass:           true
AllowShortFunctionsOnASingleLine: Inline
AllowShortIfStatementsOnASingleLine: Never
SortIncludes:           CaseInsensitive
IncludeCategories:
  - Regex:  '^<.*>'
    Priority: 1
  - Regex:  '^".*"'
    Priority: 2
```

---

## 17. Git Conventions

```
<type>(<scope>): <subject>

type:    feat | fix | refactor | perf | test | docs | chore | ci
scope:   barrier | nv9 | mqtt | ui | config | hw | build
subject: imperative, lowercase, no period, max 72 chars

Examples:
  feat(barrier): add set_slave_address() via Modbus FC06
  fix(nv9): handle SSP_EVENT_STACKER_FULL in poll loop
  refactor(mqtt): extract JSON builders into separate helpers
  test(barrier): add CRC16 test vectors from Modbus spec
  docs(readme): add Orange Pi 4 build instructions
```

---

## 18. Embedded / Real-Time Constraints

For code running on bare-metal or hard real-time paths:

```c
/* NO dynamic allocation after init */
// BAD in ISR or real-time loop:
uint8_t *buf = malloc(len);

// GOOD: pre-allocate in init, use static pools
static uint8_t rx_buf[MODBUS_MAX_FRAME];

/* NO blocking calls in ISR */
// BAD:
void UART_IRQHandler(void) {
    mutex_lock(&bus_mutex);   // BLOCKS — NEVER
}

/* Stack budget: keep frames < 512 bytes */
// Large local arrays go on heap or static storage
static uint8_t large_table[4096];   // BSS segment, not stack

/* volatile for hardware registers */
volatile uint32_t * const GPIO_BASE = (uint32_t *)0x4800A000;

/* Interrupt safety with atomic */
#include <stdatomic.h>
atomic_uint_fast32_t irq_counter = ATOMIC_VAR_INIT(0);
// In ISR:
atomic_fetch_add_explicit(&irq_counter, 1, memory_order_relaxed);
```

---

## Quick Reference

| Rule | C | C++ |
|------|---|-----|
| Error handling | `return -errno` | `[[nodiscard]] enum class Error` |
| Memory | `malloc`+`goto out`+`free` | `unique_ptr` / `vector` |
| String copy | `strlcpy` / `snprintf` | `std::string` / `string_view` |
| Threads | `pthread_mutex_t` | `std::mutex` + `std::lock_guard` |
| Atomics | `<stdatomic.h>` | `std::atomic<T>` |
| Logging | `spdlog_*` macros | `spdlog::info(...)` |
| Tests | Catch2 C API | Catch2 / GoogleTest |
| Format | clang-format Google+4 | same |
| Lint | cppcheck + clang-tidy | same |
| Sanitizers | `-fsanitize=address,undefined` | same |