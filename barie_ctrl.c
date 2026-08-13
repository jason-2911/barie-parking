/*
 * barie_ctrl.c — BLDC Road Gate Modbus RTU Controller
 *
 * Communicates via CH340/FT232 USB-RS485 adapter.
 * Protocol: Modbus RTU — BLDC Road Gate Debug v1.3
 *
 * Build:
 *   gcc -Wall -Wextra -Wpedantic -O2 \
 *       -D_FORTIFY_SOURCE=2 -fstack-protector-strong \
 *       -o barie_ctrl barie_ctrl.c
 *
 * Usage: ./barie_ctrl [OPTIONS] <command>
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/serial.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* ─── ANSI Color Codes ─── */
#define COL_RED    "\033[1;31m"
#define COL_GREEN  "\033[1;32m"
#define COL_YELLOW "\033[1;33m"
#define COL_CYAN   "\033[1;36m"
#define COL_RESET  "\033[0m"
#define COL_BOLD   "\033[1m"

/* ─── Logging Macros ─── */
#define LOG_ERR(fmt, ...)  \
    fprintf(stderr, COL_RED    "[ERR] " fmt "\n" COL_RESET, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) \
    fprintf(stderr, COL_YELLOW "[WRN] " fmt "\n" COL_RESET, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) \
    fprintf(stdout, COL_CYAN   "[INF] " fmt "\n" COL_RESET, ##__VA_ARGS__)

/* ─── Generic Helpers ─── */
#define ARRAY_SIZE(arr)  (sizeof(arr) / sizeof((arr)[0]))

/* ─── Modbus Function Codes ─── */
#define MB_FC_READ_REGS   0x03U
#define MB_FC_WRITE_REG   0x06U
#define MB_FC_WRITE_COIL  0x05U

/* ─── Coil Addresses (FC05) ─── */
#define COIL_UP         0x0000U  /* Raise gate  */
#define COIL_DOWN       0x0001U  /* Lower gate  */
#define COIL_SELF_TEST  0x0003U  /* Self-test   */

/* ─── Monitor Register Base: 0xD000 ─── */
#define REG_BASE    0xD000U
#define REG_SPEED   (REG_BASE + 0x00U)  /* Motor speed (RPM)    */
#define REG_ANGLE   (REG_BASE + 0x01U)  /* Feedback angle (deg) */
#define REG_VOLTAGE (REG_BASE + 0x02U)  /* DC bus voltage (V)   */
#define REG_CURRENT (REG_BASE + 0x03U)  /* Output current (raw) */
#define REG_STATUS  (REG_BASE + 0x14U)  /* Run status word      */
#define REG_FAULT   (REG_BASE + 0x15U)  /* Fault code           */
#define REG_PORT_IN (REG_BASE + 0x16U)  /* Digital input port   */
#define REG_VERSION (REG_BASE + 0x23U)  /* Firmware version     */

/* ─── Configuration Registers (FC06) ─── */
#define REG_SLAVE_ADDR  0x0010U  /* H00-31: Slave address */
#define REG_BAUD_RATE   0x0020U  /* H00-32: Baud rate     */

/* ─── Protocol Limits ─── */
#define MB_MAX_READ_REGS  125U   /* FC03 single-request cap (Modbus spec) */
#define MB_MIN_SLAVE_ID     1U
#define MB_MAX_SLAVE_ID   247U

/* ─── Timeouts ─── */
#define MODBUS_TIMEOUT_MS       500
#define MODBUS_INTER_FRAME_US   10000U  /* 10 ms — RTU silent gap */
#define SCAN_TIMEOUT_MS         200

/* ─── Status Word Bit Masks (REG_STATUS / D014H) ─── */
#define STS_RED_LIGHT     (1U << 13)  /* Red LED output active   */
#define STS_GREEN_LIGHT   (1U << 12)  /* Green LED output active */
#define STS_LIMIT_OUT     (1U << 11)  /* Limit output active     */
#define STS_UP_LIMIT      (1U << 10)  /* Raised-to-limit         */
#define STS_DOWN_LIMIT    (1U <<  9)  /* Lowered-to-limit        */
#define STS_SELF_TEST_ERR (1U <<  6)  /* Self-test error         */
#define STS_SELF_TESTED   (1U <<  5)  /* Self-test completed     */
#define STS_NORMAL_POWER  (1U <<  4)  /* Normal power-on         */
#define STS_POWER_DOWN    (1U <<  2)  /* Power loss detected     */
#define STS_DIR           (1U <<  1)  /* Motor direction (1=up)  */
#define STS_RUNNING       (1U <<  0)  /* Motor running           */

/* ─── Fault Code Description Table ─── */
static const char * const fault_desc[] = {
    [0] = "No fault",
    [1] = "Overcurrent",
    [2] = "Overvoltage",
    [3] = "Undervoltage",
    [4] = "Overtemperature",
    [5] = "Self-test failure",
    [6] = "Hall sensor fault",
};

/* ─── Modbus Context ─── */
typedef struct {
    int      fd;
    uint8_t  slave_id;
    int      timeout_ms;
    bool     verbose;
    bool     rs485_kernel;  /* Kernel owns RTS — skip manual toggle */
} modbus_ctx_t;

/* ══════════════════════════════════════════════════════════
 *  crc16_modbus — CRC-16/Modbus (polynomial 0xA001, init 0xFFFF)
 * ══════════════════════════════════════════════════════════ */
static uint16_t crc16_modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;

    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001U)
                crc = (crc >> 1) ^ 0xA001U;
            else
                crc >>= 1;
        }
    }
    return crc;
}

/* ══════════════════════════════════════════════════════════
 *  rts_set — Assert or de-assert RTS (DE/RE pin on MAX485)
 *
 *  Returns 0 on success, -1 on ioctl failure.
 * ══════════════════════════════════════════════════════════ */
static int rts_set(int fd, bool assert_rts)
{
    int flags = 0;

    if (ioctl(fd, TIOCMGET, &flags) < 0) {
        LOG_WARN("TIOCMGET failed: %s", strerror(errno));
        return -1;
    }

    if (assert_rts)
        flags |= TIOCM_RTS;
    else
        flags &= ~TIOCM_RTS;

    if (ioctl(fd, TIOCMSET, &flags) < 0) {
        LOG_WARN("TIOCMSET failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

/* ══════════════════════════════════════════════════════════
 *  serial_rs485_enable — Request kernel-managed RS485 mode
 *
 *  CH340 does not support TIOCSRS485; the ioctl will fail and
 *  the caller falls back to manual RTS toggling.
 *
 *  Returns true if the kernel accepted the RS485 configuration.
 * ══════════════════════════════════════════════════════════ */
static bool serial_rs485_enable(int fd)
{
    struct serial_rs485 rs485;

    memset(&rs485, 0, sizeof(rs485));
    rs485.flags                 = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND;
    rs485.delay_rts_before_send = 0U;
    rs485.delay_rts_after_send  = 2U;  /* ms — hold DE after last byte */

    if (ioctl(fd, TIOCSRS485, &rs485) < 0) {
        LOG_WARN("TIOCSRS485 not supported (%s) — using manual RTS",
                 strerror(errno));
        return false;
    }

    LOG_INFO("Kernel RS485 mode active (kernel controls RTS/DE)");
    return true;
}

/* ══════════════════════════════════════════════════════════
 *  serial_open — Open and configure a serial port (8N1, raw)
 *
 *  Returns fd >= 0 on success, -1 on error.
 * ══════════════════════════════════════════════════════════ */
static int serial_open(const char *port, int baud)
{
    if (!port) {
        LOG_ERR("serial_open: null port");
        return -1;
    }

    speed_t speed;
    switch (baud) {
    case 9600:   speed = B9600;   break;
    case 19200:  speed = B19200;  break;
    case 38400:  speed = B38400;  break;
    case 57600:  speed = B57600;  break;
    case 115200: speed = B115200; break;
    default:
        LOG_ERR("Unsupported baud rate: %d", baud);
        return -1;
    }

    int fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        LOG_ERR("Cannot open %s: %s", port, strerror(errno));
        return -1;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    if (tcgetattr(fd, &tty) != 0) {
        LOG_ERR("tcgetattr(%s): %s", port, strerror(errno));
        goto err_close;
    }

    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    /* 8N1, raw mode, no hardware flow control */
    tty.c_cflag  = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    tty.c_cflag |= (CREAD | CLOCAL);
    tty.c_iflag  = 0;
    tty.c_oflag  = 0;
    tty.c_lflag  = 0;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        LOG_ERR("tcsetattr(%s): %s", port, strerror(errno));
        goto err_close;
    }

    /* Start with RTS low — adapter in receive mode */
    rts_set(fd, false);
    return fd;

err_close:
    close(fd);
    return -1;
}

/* ══════════════════════════════════════════════════════════
 *  serial_read_timeout — Collect bytes up to max_len within
 *  timeout_ms, stopping early on the Modbus inter-frame gap.
 *
 *  Returns number of bytes received, 0 on timeout/empty, -1 on error.
 * ══════════════════════════════════════════════════════════ */
static ssize_t serial_read_timeout(int fd, uint8_t *buf, size_t max_len,
                                   int timeout_ms)
{
    if (!buf || max_len == 0) {
        LOG_ERR("serial_read_timeout: invalid buffer");
        return -1;
    }

    size_t total = 0;
    struct timeval deadline, now, remaining;

    gettimeofday(&deadline, NULL);
    deadline.tv_sec  += timeout_ms / 1000;
    deadline.tv_usec += (timeout_ms % 1000) * 1000;
    if (deadline.tv_usec >= 1000000L) {
        deadline.tv_sec++;
        deadline.tv_usec -= 1000000L;
    }

    while (total < max_len) {
        gettimeofday(&now, NULL);
        timersub(&deadline, &now, &remaining);
        if (remaining.tv_sec < 0)
            break;  /* Deadline passed */

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        int sel = select(fd + 1, &rfds, NULL, NULL, &remaining);
        if (sel < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (sel == 0)
            break;  /* Timeout — no more data */

        ssize_t n = read(fd, buf + total, max_len - total);
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            break;
        }
        if (n == 0)
            break;  /* EOF */

        total += (size_t)n;

        /*
         * After receiving the first chunk, wait one Modbus inter-frame
         * silent interval to decide whether the frame is complete.
         * If no further bytes arrive within that window, stop reading.
         */
        struct timeval ift = {
            .tv_sec  = 0,
            .tv_usec = (long)MODBUS_INTER_FRAME_US,
        };
        fd_set rfd2;
        FD_ZERO(&rfd2);
        FD_SET(fd, &rfd2);
        if (select(fd + 1, &rfd2, NULL, NULL, &ift) <= 0)
            break;
    }

    return (ssize_t)total;
}

/* ─── Debug frame dump (verbose mode) ─── */
static void print_frame(const char *label, const uint8_t *buf, size_t len)
{
    if (!label || !buf)
        return;

    printf(COL_CYAN "  %-4s [%2zu]: " COL_RESET, label, len);
    for (size_t i = 0; i < len; i++)
        printf("%02X ", buf[i]);
    putchar('\n');
}

/* ══════════════════════════════════════════════════════════
 *  modbus_transaction — Send a request frame and receive the
 *  corresponding response, verifying CRC and slave address.
 *
 *  Returns number of response bytes on success, -1 on error.
 * ══════════════════════════════════════════════════════════ */
static int modbus_transaction(modbus_ctx_t *ctx,
                              const uint8_t *req, size_t req_len,
                              uint8_t *resp, size_t resp_max)
{
    if (!ctx || !req || !resp || req_len == 0 || resp_max == 0) {
        LOG_ERR("modbus_transaction: invalid argument");
        return -1;
    }

    /* Purge any stale data before transmitting */
    tcflush(ctx->fd, TCIOFLUSH);

    if (ctx->verbose)
        print_frame("TX", req, req_len);

    /*
     * Manual RTS/DE direction control:
     *   - Enabled only when the kernel RS485 driver is unavailable
     *     (e.g. CH340 rejects TIOCSRS485).
     *   - If rs485_kernel is true the driver owns the pin; calling
     *     rts_set() would race with the driver and corrupt timing.
     */
    if (!ctx->rs485_kernel) {
        rts_set(ctx->fd, true);
        usleep(1000U);  /* Wait for MAX485 DE to settle */
    }

    ssize_t nwr = write(ctx->fd, req, req_len);
    if (nwr < 0) {
        LOG_ERR("write() failed: %s", strerror(errno));
        if (!ctx->rs485_kernel)
            rts_set(ctx->fd, false);
        return -1;
    }
    if ((size_t)nwr != req_len) {
        LOG_ERR("Short write: expected %zu bytes, wrote %zd", req_len, nwr);
        if (!ctx->rs485_kernel)
            rts_set(ctx->fd, false);
        return -1;
    }

    tcdrain(ctx->fd);  /* Block until UART TX FIFO is empty */

    if (!ctx->rs485_kernel) {
        rts_set(ctx->fd, false);
        usleep(1000U);  /* Allow MAX485 to switch back to receive mode */
    }

    ssize_t nrd = serial_read_timeout(ctx->fd, resp, resp_max, ctx->timeout_ms);
    if (nrd <= 0) {
        LOG_ERR("Timeout — no response from slave 0x%02X", ctx->slave_id);
        return -1;
    }

    if (ctx->verbose)
        print_frame("RX", resp, (size_t)nrd);

    /* Minimum valid RTU frame: addr(1) + FC(1) + data(≥1) + CRC(2) */
    if (nrd < 4) {
        LOG_ERR("Frame too short: %zd bytes (minimum 4)", nrd);
        return -1;
    }

    /*
     * Verify CRC — Modbus RTU appends CRC little-endian (LSB first):
     *   frame[n-2] = CRC_LO,  frame[n-1] = CRC_HI
     */
    uint16_t crc_recv = ((uint16_t)resp[nrd - 1] << 8) |
                         (uint16_t)resp[nrd - 2];
    uint16_t crc_calc = crc16_modbus(resp, (size_t)nrd - 2);
    if (crc_recv != crc_calc) {
        LOG_ERR("CRC mismatch: received 0x%04X, calculated 0x%04X",
                crc_recv, crc_calc);
        return -1;
    }

    /* Verify the response comes from the expected slave */
    if (resp[0] != ctx->slave_id) {
        LOG_ERR("Slave ID mismatch: expected 0x%02X, got 0x%02X",
                ctx->slave_id, resp[0]);
        return -1;
    }

    /* Detect Modbus exception response (FC with bit 7 set) */
    if (resp[1] & 0x80U) {
        LOG_ERR("Modbus exception: FC=0x%02X, exception code=0x%02X",
                resp[1] & 0x7FU, resp[2]);
        return -1;
    }

    return (int)nrd;
}

/* ══════════════════════════════════════════════════════════
 *  modbus_write_coil — FC05: Write Single Coil
 *
 *  Returns 0 on success, -1 on error.
 * ══════════════════════════════════════════════════════════ */
static int modbus_write_coil(modbus_ctx_t *ctx, uint16_t coil_addr)
{
    if (!ctx)
        return -1;

    uint8_t req[8];
    req[0] = ctx->slave_id;
    req[1] = MB_FC_WRITE_COIL;
    req[2] = (uint8_t)(coil_addr >> 8);
    req[3] = (uint8_t)(coil_addr & 0xFFU);
    req[4] = 0xFFU;  /* Coil ON */
    req[5] = 0x00U;

    uint16_t crc = crc16_modbus(req, 6U);
    req[6] = (uint8_t)(crc & 0xFFU);
    req[7] = (uint8_t)(crc >> 8);

    uint8_t resp[8];
    int n = modbus_transaction(ctx, req, sizeof(req), resp, sizeof(resp));
    if (n < 0)
        return -1;

    /* FC05 response must echo the full request (8 bytes) */
    if (n < (int)sizeof(req)) {
        LOG_ERR("FC05 echo too short: got %d bytes, expected %zu", n, sizeof(req));
        return -1;
    }
    if (memcmp(req, resp, 6U) != 0) {
        LOG_ERR("FC05 echo mismatch (bytes 0–5)");
        return -1;
    }
    return 0;
}

/* ══════════════════════════════════════════════════════════
 *  modbus_write_register — FC06: Write Single Register
 *
 *  Returns 0 on success, -1 on error.
 * ══════════════════════════════════════════════════════════ */
static int modbus_write_register(modbus_ctx_t *ctx,
                                 uint16_t reg_addr, uint16_t value)
{
    if (!ctx)
        return -1;

    uint8_t req[8];
    req[0] = ctx->slave_id;
    req[1] = MB_FC_WRITE_REG;
    req[2] = (uint8_t)(reg_addr >> 8);
    req[3] = (uint8_t)(reg_addr & 0xFFU);
    req[4] = (uint8_t)(value >> 8);
    req[5] = (uint8_t)(value & 0xFFU);

    uint16_t crc = crc16_modbus(req, 6U);
    req[6] = (uint8_t)(crc & 0xFFU);
    req[7] = (uint8_t)(crc >> 8);

    uint8_t resp[8];
    int n = modbus_transaction(ctx, req, sizeof(req), resp, sizeof(resp));
    if (n < 0)
        return -1;

    /* FC06 response must echo the full request */
    if (n < (int)sizeof(req)) {
        LOG_ERR("FC06 echo too short: got %d bytes, expected %zu", n, sizeof(req));
        return -1;
    }
    if (memcmp(req, resp, 6U) != 0) {
        LOG_ERR("FC06 echo mismatch (bytes 0–5)");
        return -1;
    }
    return 0;
}

/* ══════════════════════════════════════════════════════════
 *  modbus_read_registers — FC03: Read Holding Registers
 *
 *  Reads @count registers starting at @start_addr into @out_regs.
 *  Returns number of registers read on success, -1 on error.
 * ══════════════════════════════════════════════════════════ */
static int modbus_read_registers(modbus_ctx_t *ctx,
                                 uint16_t start_addr, uint16_t count,
                                 uint16_t *out_regs)
{
    if (!ctx || !out_regs) {
        LOG_ERR("modbus_read_registers: invalid argument");
        return -1;
    }
    if (count == 0 || count > MB_MAX_READ_REGS) {
        LOG_ERR("modbus_read_registers: count %u out of range [1, %u]",
                count, MB_MAX_READ_REGS);
        return -1;
    }

    uint8_t req[8];
    req[0] = ctx->slave_id;
    req[1] = MB_FC_READ_REGS;
    req[2] = (uint8_t)(start_addr >> 8);
    req[3] = (uint8_t)(start_addr & 0xFFU);
    req[4] = (uint8_t)(count >> 8);
    req[5] = (uint8_t)(count & 0xFFU);

    uint16_t crc = crc16_modbus(req, 6U);
    req[6] = (uint8_t)(crc & 0xFFU);
    req[7] = (uint8_t)(crc >> 8);

    /* Response: addr(1) + FC(1) + byte_count(1) + data(2×count) + CRC(2) */
    size_t resp_len = 5U + (size_t)count * 2U;
    uint8_t *resp = malloc(resp_len);
    if (!resp) {
        LOG_ERR("malloc(%zu) failed: %s", resp_len, strerror(errno));
        return -1;
    }

    int n = modbus_transaction(ctx, req, sizeof(req), resp, resp_len);
    if (n < 0)
        goto fail;

    /* Validate the byte count field in the response */
    uint8_t byte_count = resp[2];
    if (byte_count != (uint8_t)(count * 2U)) {
        LOG_ERR("FC03 byte_count: expected %u, got %u",
                (unsigned)(count * 2U), (unsigned)byte_count);
        goto fail;
    }

    /* Ensure the response is long enough to hold the advertised data */
    if ((size_t)n < (3U + (size_t)byte_count + 2U)) {
        LOG_ERR("FC03 response truncated: %d bytes (need %zu)",
                n, 3U + (size_t)byte_count + 2U);
        goto fail;
    }

    for (uint16_t i = 0; i < count; i++) {
        out_regs[i] = ((uint16_t)resp[3U + i * 2U] << 8) |
                       (uint16_t)resp[4U + i * 2U];
    }

    free(resp);
    return (int)count;

fail:
    free(resp);
    return -1;
}

/* ══════════════════════════════════════════════════════════
 *  print_status — Decode and display the status word
 * ══════════════════════════════════════════════════════════ */
static void print_status(uint16_t status, uint16_t fault, uint16_t port_in)
{
    printf("\n" COL_BOLD "─── Gate Status ─────────────────────────────────\n"
           COL_RESET);

    /* Motion state */
    printf("  Motion     : ");
    if (status & STS_RUNNING)
        printf(COL_GREEN "Running" COL_RESET " — direction: %s\n",
               (status & STS_DIR) ? "raise" : "lower");
    else
        printf(COL_YELLOW "Stopped\n" COL_RESET);

    /* Position limits */
    bool at_up   = !!(status & STS_UP_LIMIT);
    bool at_down = !!(status & STS_DOWN_LIMIT);
    printf("  Position   : ");
    if (at_up && at_down)
        printf(COL_YELLOW "Both limits active — check wiring\n" COL_RESET);
    else if (at_up)
        printf(COL_GREEN "Fully raised\n" COL_RESET);
    else if (at_down)
        printf(COL_GREEN "Fully lowered\n" COL_RESET);
    else
        printf(COL_YELLOW "Mid-travel\n" COL_RESET);

    /* Self-test result */
    printf("  Self-test  : ");
    if (!(status & STS_SELF_TESTED))
        printf(COL_YELLOW "Not yet run\n" COL_RESET);
    else if (status & STS_SELF_TEST_ERR)
        printf(COL_RED "Completed — FAULT\n" COL_RESET);
    else
        printf(COL_GREEN "Completed — OK\n" COL_RESET);

    /* Power supply */
    printf("  Power      : %s\n",
           (status & STS_NORMAL_POWER)
               ? COL_GREEN "Normal" COL_RESET
               : COL_RED   "Abnormal" COL_RESET);

    /* Indicator LEDs */
    printf("  Lights     : ");
    if (!(status & (STS_GREEN_LIGHT | STS_RED_LIGHT))) {
        printf("Off\n");
    } else {
        if (status & STS_GREEN_LIGHT) printf(COL_GREEN "Green " COL_RESET);
        if (status & STS_RED_LIGHT)   printf(COL_RED   "Red"   COL_RESET);
        putchar('\n');
    }

    /* Digital inputs */
    printf("  Inputs     : IO_UP=%u  IO_DOWN=%u  RC_UP=%u  RC_DOWN=%u\n",
           !!(port_in & (1U << 0)),
           !!(port_in & (1U << 1)),
           !!(port_in & (1U << 8)),
           !!(port_in & (1U << 9)));

    /* Fault code */
    printf("  Fault      : ");
    if (fault == 0) {
        printf(COL_GREEN "0 — No fault\n" COL_RESET);
    } else {
        const char *desc = (fault < ARRAY_SIZE(fault_desc))
                           ? fault_desc[fault]
                           : "Unknown fault code";
        printf(COL_RED "%u — %s\n" COL_RESET, fault, desc);
    }

    printf(COL_BOLD "─────────────────────────────────────────────────\n"
           COL_RESET);
}

/* ══════════════════════════════════════════════════════════
 *  cmd_status — Read and display all device parameters
 * ══════════════════════════════════════════════════════════ */
static int cmd_status(modbus_ctx_t *ctx)
{
    if (!ctx)
        return -1;

    uint16_t basic[4];
    if (modbus_read_registers(ctx, REG_SPEED, 4U, basic) < 0) {
        LOG_ERR("Failed to read basic parameters (speed/angle/voltage/current)");
        return -1;
    }

    printf("\n" COL_BOLD "─── Technical Parameters ────────────────────────\n"
           COL_RESET);
    printf("  Speed      : %u RPM\n",   basic[0]);
    printf("  Angle      : %u deg\n",   basic[1]);
    printf("  Bus voltage: %u V\n",     basic[2]);
    printf("  Current    : %u (raw)\n", basic[3]);

    uint16_t sts[3];
    if (modbus_read_registers(ctx, REG_STATUS, 3U, sts) < 0) {
        LOG_ERR("Failed to read status registers");
        return -1;
    }

    uint16_t ver = 0;
    if (modbus_read_registers(ctx, REG_VERSION, 1U, &ver) == 1)
        printf("  Firmware   : v%u.%u\n", ver >> 8, ver & 0xFFU);

    print_status(sts[0], sts[1], sts[2]);
    return 0;
}

/* ══════════════════════════════════════════════════════════
 *  cmd_up / cmd_down / cmd_selftest
 * ══════════════════════════════════════════════════════════ */
static int cmd_up(modbus_ctx_t *ctx)
{
    if (!ctx)
        return -1;

    printf(COL_BOLD "↑  Raising gate (slave 0x%02X)...\n" COL_RESET,
           ctx->slave_id);

    if (modbus_write_coil(ctx, COIL_UP) < 0) {
        LOG_ERR("Raise command failed");
        return -1;
    }

    printf(COL_GREEN "✓  Raise command sent\n" COL_RESET);
    return 0;
}

static int cmd_down(modbus_ctx_t *ctx)
{
    if (!ctx)
        return -1;

    printf(COL_BOLD "↓  Lowering gate (slave 0x%02X)...\n" COL_RESET,
           ctx->slave_id);

    if (modbus_write_coil(ctx, COIL_DOWN) < 0) {
        LOG_ERR("Lower command failed");
        return -1;
    }

    printf(COL_GREEN "✓  Lower command sent\n" COL_RESET);
    return 0;
}

static int cmd_selftest(modbus_ctx_t *ctx)
{
    if (!ctx)
        return -1;

    printf(COL_BOLD "⚙  Self-test (slave 0x%02X)...\n" COL_RESET, ctx->slave_id);

    if (modbus_write_coil(ctx, COIL_SELF_TEST) < 0) {
        LOG_ERR("Self-test command failed");
        return -1;
    }

    printf(COL_GREEN "✓  Self-test command sent\n" COL_RESET);
    return 0;
}

static int cmd_setaddr(modbus_ctx_t *ctx, unsigned int new_id)
{
    if (!ctx)
        return -1;

    if (new_id < MB_MIN_SLAVE_ID || new_id > MB_MAX_SLAVE_ID) {
        LOG_ERR("Slave ID %u out of range [%u, %u]",
                new_id, MB_MIN_SLAVE_ID, MB_MAX_SLAVE_ID);
        return -1;
    }

    printf(COL_BOLD "  Setting slave address: %u → %u\n" COL_RESET,
           (unsigned)ctx->slave_id, new_id);

    /*
     * This firmware applies the new address immediately:
     * request goes out with old_id, response comes back with new_id.
     * Build the request manually with old_id, then temporarily set
     * ctx->slave_id = new_id so modbus_transaction validates the
     * response slave byte against the correct (new) address.
     */
    uint8_t req[8];
    req[0] = ctx->slave_id;             /* Send TO old address   */
    req[1] = MB_FC_WRITE_REG;
    req[2] = (uint8_t)(REG_SLAVE_ADDR >> 8);
    req[3] = (uint8_t)(REG_SLAVE_ADDR & 0xFFU);
    req[4] = (uint8_t)(new_id >> 8);
    req[5] = (uint8_t)(new_id & 0xFFU);
    uint16_t crc = crc16_modbus(req, 6U);
    req[6] = (uint8_t)(crc & 0xFFU);
    req[7] = (uint8_t)(crc >> 8);

    uint8_t old_id    = ctx->slave_id;
    ctx->slave_id     = (uint8_t)new_id; /* Expect response FROM new address */

    uint8_t resp[8];
    int n = modbus_transaction(ctx, req, sizeof(req), resp, sizeof(resp));

    ctx->slave_id = old_id;             /* Restore for caller    */

    if (n < 0) {
        LOG_ERR("Failed to write slave address register");
        return -1;
    }

    printf(COL_GREEN "✓  Slave address updated to %u\n" COL_RESET, new_id);
    printf(COL_YELLOW "  ⚠  Use -a %u for all subsequent commands\n" COL_RESET,
           new_id);
    return 0;
}

/* ══════════════════════════════════════════════════════════
 *  cmd_scan — Probe all valid slave addresses
 * ══════════════════════════════════════════════════════════ */
static int cmd_scan(modbus_ctx_t *ctx)
{
    if (!ctx)
        return -1;

    printf(COL_BOLD "\n🔍 Scanning Modbus addresses %u–%u...\n" COL_RESET,
           MB_MIN_SLAVE_ID, MB_MAX_SLAVE_ID);
    printf("   Timeout: %d ms per address\n\n", SCAN_TIMEOUT_MS);

    int  orig_timeout  = ctx->timeout_ms;
    bool orig_verbose  = ctx->verbose;
    ctx->timeout_ms    = SCAN_TIMEOUT_MS;
    ctx->verbose       = false;

    int found = 0;

    for (unsigned int a = MB_MIN_SLAVE_ID; a <= MB_MAX_SLAVE_ID; a++) {
        ctx->slave_id = (uint8_t)a;

        printf("  [%3u] ", a);
        fflush(stdout);

        uint16_t sts = 0;
        if (modbus_read_registers(ctx, REG_STATUS, 1U, &sts) > 0) {
            uint16_t ver = 0;
            modbus_read_registers(ctx, REG_VERSION, 1U, &ver);
            printf(COL_GREEN "Found!  " COL_RESET
                   "status=0x%04X  fw=v%u.%u\n",
                   sts, ver >> 8, ver & 0xFFU);
            found++;
        } else {
            printf("—\n");
        }

        usleep(10000U);
    }

    ctx->timeout_ms = orig_timeout;
    ctx->verbose    = orig_verbose;

    printf("\n" COL_BOLD "Scan complete: %d device(s) found\n" COL_RESET, found);
    return (found > 0) ? 0 : 1;
}

/* ══════════════════════════════════════════════════════════
 *  cmd_monitor — Poll and display live telemetry
 * ══════════════════════════════════════════════════════════ */
static int cmd_monitor(modbus_ctx_t *ctx, int interval_ms)
{
    if (!ctx) {
        LOG_ERR("cmd_monitor: null context");
        return -1;
    }
    if (interval_ms <= 0) {
        LOG_ERR("cmd_monitor: invalid interval %d ms", interval_ms);
        return -1;
    }

    printf(COL_BOLD "📊 Live monitor (Ctrl-C to quit)\n" COL_RESET);
    printf("   Slave 0x%02X  interval %d ms\n\n", ctx->slave_id, interval_ms);

    while (1) {
        printf("\033[H\033[2J");
        printf(COL_BOLD COL_CYAN "=== GATE MONITOR — Slave 0x%02X ===\n"
               COL_RESET, ctx->slave_id);

        uint16_t basic[4];
        if (modbus_read_registers(ctx, REG_SPEED, 4U, basic) == 4) {
            printf("Speed: %4u RPM  Angle: %3u°  Voltage: %3u V  Current: %u\n",
                   basic[0], basic[1], basic[2], basic[3]);
        } else {
            printf(COL_RED "Failed to read basic parameters\n" COL_RESET);
        }

        uint16_t sts[3];
        if (modbus_read_registers(ctx, REG_STATUS, 3U, sts) == 3)
            print_status(sts[0], sts[1], sts[2]);
        else
            printf(COL_RED "Connection lost!\n" COL_RESET);

        usleep((useconds_t)interval_ms * 1000U);
    }

    return 0;  /* Unreachable — exits via SIGINT */
}

/* ══════════════════════════════════════════════════════════
 *  cmd_looptest — Physical RS485 loopback test
 *
 *  Requires two adapters wired A↔A and B↔B.
 *  TX is on fd_tx, RX on fd_rx.
 * ══════════════════════════════════════════════════════════ */
static int cmd_looptest(int fd_tx, int fd_rx, bool verbose)
{
    /* Reject same-port loopback — half-duplex cannot self-echo */
    struct stat st_tx, st_rx;
    if (fstat(fd_tx, &st_tx) == 0 && fstat(fd_rx, &st_rx) == 0 &&
        st_tx.st_ino == st_rx.st_ino) {
        LOG_ERR("TX and RX are the same device node");
        fprintf(stderr,
                "       RS485 half-duplex: DE=1 during TX disables the receiver\n"
                "       on the same adapter.  Wire two adapters A↔A and B↔B:\n"
                "         ./barie_ctrl -p /dev/ttyUSB0 looptest /dev/ttyUSB1\n");
        return -1;
    }

    const int rounds = 5;
    int pass = 0;

    printf(COL_BOLD "🔁 RS485 Looptest — %d rounds\n" COL_RESET, rounds);
    printf("   TX → RX,  byte-by-byte CRC-verified\n\n");

    for (int i = 0; i < rounds; i++) {
        /* Vary byte[2] each round to detect stale data from a previous round */
        uint8_t frame[8] = {
            0xAAU, 0xBBU, (uint8_t)(i + 1), 0x11U, 0x22U, 0x33U, 0U, 0U
        };
        uint16_t crc = crc16_modbus(frame, 6U);
        frame[6] = (uint8_t)(crc & 0xFFU);
        frame[7] = (uint8_t)(crc >> 8);

        tcflush(fd_rx, TCIFLUSH);

        if (verbose)
            print_frame("TX", frame, sizeof(frame));

        /* Assert DE, transmit, wait for FIFO drain, release DE */
        rts_set(fd_tx, true);
        usleep(500U);

        ssize_t nwr = write(fd_tx, frame, sizeof(frame));
        tcdrain(fd_tx);
        rts_set(fd_tx, false);

        if (nwr != (ssize_t)sizeof(frame)) {
            printf(COL_RED "  [%d] FAIL — write error: %s\n" COL_RESET,
                   i + 1, strerror(errno));
            continue;
        }

        uint8_t rxbuf[16];
        ssize_t nrd = serial_read_timeout(fd_rx, rxbuf, sizeof(rxbuf), 300);

        if (verbose && nrd > 0)
            print_frame("RX", rxbuf, (size_t)nrd);

        if (nrd == (ssize_t)sizeof(frame) &&
            memcmp(frame, rxbuf, sizeof(frame)) == 0) {
            printf(COL_GREEN "  [%d] PASS\n" COL_RESET, i + 1);
            pass++;
        } else if (nrd <= 0) {
            printf(COL_RED "  [%d] FAIL — timeout, no bytes received\n"
                   COL_RESET, i + 1);
        } else {
            printf(COL_RED "  [%d] FAIL — received %zd bytes, data mismatch\n"
                   COL_RESET, i + 1, nrd);
            if (verbose)
                print_frame("EXP", frame, sizeof(frame));
        }

        usleep(20000U);
    }

    printf("\n" COL_BOLD "Looptest result: %d/%d PASS\n" COL_RESET, pass, rounds);
    return (pass == rounds) ? 0 : 1;
}

/* ─── Usage ─── */
static void print_usage(const char *prog)
{
    if (!prog)
        prog = "barie_ctrl";

    printf(COL_BOLD COL_CYAN
           "╔════════════════════════════════════════════════════╗\n"
           "║    BARIE Modbus RTU Controller  v1.1               ║\n"
           "║    BLDC Road Gate — Modbus RTU Debug 1.3           ║\n"
           "╚════════════════════════════════════════════════════╝\n"
           COL_RESET "\n");

    printf("Usage: %s [OPTIONS] <command> [args...]\n\n", prog);

    printf(COL_BOLD "OPTIONS\n" COL_RESET);
    printf("  -p <port>    Serial port          (default: /dev/ttyUSB0)\n");
    printf("  -b <baud>    Baud rate            (default: 9600)\n");
    printf("  -a <id>      Modbus slave ID      (default: 1)\n");
    printf("  -t <ms>      Response timeout ms  (default: %d)\n",
           MODBUS_TIMEOUT_MS);
    printf("  -v           Verbose hex dump TX/RX\n");
    printf("  -h           Show this help\n\n");

    printf(COL_BOLD "COMMANDS\n" COL_RESET);
    printf("  scan               Probe all slave IDs (%u–%u)\n",
           MB_MIN_SLAVE_ID, MB_MAX_SLAVE_ID);
    printf("  up                 Raise gate\n");
    printf("  down               Lower gate\n");
    printf("  status             Read current status\n");
    printf("  selftest           Trigger self-test sequence\n");
    printf("  setaddr <N>        Set slave ID (1–%u)\n", MB_MAX_SLAVE_ID);
    printf("  monitor [ms]       Live monitor, refresh interval ms (default 1000)\n");
    printf("  looptest <rxport>  Physical RS485 loopback test (requires 2 adapters)\n\n");

    printf(COL_BOLD "EXAMPLES\n" COL_RESET);
    printf("  %s scan\n", prog);
    printf("  %s -a 1 up\n", prog);
    printf("  %s -a 1 -v status\n", prog);
    printf("  %s -p /dev/ttyUSB0 -b 9600 -a 1 status\n", prog);
    printf("  %s -a 1 monitor 500\n", prog);
    printf("  %s -p /dev/ttyUSB0 looptest /dev/ttyUSB1\n\n", prog);
}

/* ══════════════════════════════════════════════════════════
 *  main
 * ══════════════════════════════════════════════════════════ */
int main(int argc, char *argv[])
{
    const char *port     = "/dev/ttyUSB0";
    int         baud     = 19200;
    uint8_t     slave_id = 1U;
    int         timeout  = MODBUS_TIMEOUT_MS;
    bool        verbose  = false;

    int opt;
    while ((opt = getopt(argc, argv, "p:b:a:t:vh")) != -1) {
        switch (opt) {
        case 'p':
            port = optarg;
            break;

        case 'b':
            baud = atoi(optarg);
            if (baud <= 0) {
                LOG_ERR("Invalid baud rate: '%s'", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'a': {
            int a = atoi(optarg);
            if (a < (int)MB_MIN_SLAVE_ID || a > (int)MB_MAX_SLAVE_ID) {
                LOG_ERR("Slave ID %d out of range [%u, %u]",
                        a, MB_MIN_SLAVE_ID, MB_MAX_SLAVE_ID);
                return EXIT_FAILURE;
            }
            slave_id = (uint8_t)a;
            break;
        }

        case 't':
            timeout = atoi(optarg);
            if (timeout <= 0) {
                LOG_ERR("Invalid timeout: '%s'", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'v':
            verbose = true;
            break;

        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;

        default:
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (optind >= argc) {
        LOG_ERR("No command specified");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *cmd = argv[optind];

    int fd = serial_open(port, baud);
    if (fd < 0)
        return EXIT_FAILURE;

    LOG_INFO("Port: %s  baud: %d  slave: 0x%02X", port, baud, slave_id);

    bool rs485_kernel = serial_rs485_enable(fd);

    modbus_ctx_t ctx = {
        .fd           = fd,
        .slave_id     = slave_id,
        .timeout_ms   = timeout,
        .verbose      = verbose,
        .rs485_kernel = rs485_kernel,
    };

    int ret;

    if (strcmp(cmd, "scan") == 0) {
        ret = cmd_scan(&ctx);

    } else if (strcmp(cmd, "up") == 0) {
        ret = cmd_up(&ctx);

    } else if (strcmp(cmd, "down") == 0) {
        ret = cmd_down(&ctx);

    } else if (strcmp(cmd, "status") == 0) {
        ret = cmd_status(&ctx);

    } else if (strcmp(cmd, "selftest") == 0) {
        ret = cmd_selftest(&ctx);

    } else if (strcmp(cmd, "setaddr") == 0) {
        if (optind + 1 >= argc) {
            LOG_ERR("setaddr requires an argument: setaddr <1-%u>",
                    MB_MAX_SLAVE_ID);
            ret = EXIT_FAILURE;
        } else {
            ret = cmd_setaddr(&ctx, (unsigned int)atoi(argv[optind + 1]));
        }

    } else if (strcmp(cmd, "monitor") == 0) {
        int interval = 1000;
        if (optind + 1 < argc) {
            interval = atoi(argv[optind + 1]);
            if (interval <= 0) {
                LOG_ERR("Invalid monitor interval: '%s'", argv[optind + 1]);
                close(fd);
                return EXIT_FAILURE;
            }
        }
        ret = cmd_monitor(&ctx, interval);

    } else if (strcmp(cmd, "looptest") == 0) {
        if (optind + 1 >= argc) {
            LOG_ERR("looptest requires an RX port argument");
            fprintf(stderr, "  Usage: %s -p <txport> looptest <rxport>\n",
                    argv[0]);
            ret = EXIT_FAILURE;
        } else {
            int fd_rx = serial_open(argv[optind + 1], baud);
            if (fd_rx < 0) {
                ret = EXIT_FAILURE;
            } else {
                ret = cmd_looptest(fd, fd_rx, verbose);
                close(fd_rx);
            }
        }

    } else {
        LOG_ERR("Unknown command: '%s'", cmd);
        print_usage(argv[0]);
        ret = EXIT_FAILURE;
    }

    close(fd);
    return (ret == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}