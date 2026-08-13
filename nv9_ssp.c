/*
 * nv9_ssp.c - NV9 Banknote Validator SSP driver
 *
 * Build:
 *   gcc -Wall -Wextra -O2 -o nv9_ssp nv9_ssp.c
 *
 * Run (normal):
 *   sudo ./nv9_ssp /dev/ttyACM0
 *
 * Run (debug - show raw TX/RX frames):
 *   sudo ./nv9_ssp /dev/ttyACM0 -d
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CMD_SYNC            0x11
#define CMD_SETUP_REQUEST   0x05
#define CMD_CH_INHIBITS     0x02
#define CMD_ENABLE          0x0A
#define CMD_DISABLE         0x09
#define CMD_POLL            0x07

#define RESP_OK             0xF0

#define EVT_SLAVE_RESET     0xF1
#define EVT_DISABLED        0xE8
#define EVT_NOTE_READ       0xEF
#define EVT_NOTE_CREDIT     0xEE
#define EVT_NOTE_REJECTED   0xED
#define EVT_NOTE_STACKING   0xCC
#define EVT_NOTE_STACKED    0xEB
#define EVT_NOTE_RETURNING  0xEC
#define EVT_SAFE_JAM        0xEA
#define EVT_UNSAFE_JAM      0xE9
#define EVT_STACKER_FULL    0xE7
#define EVT_INITIALIZING    0xB6

#define STX                 0x7F
#define SSP_ADDR            0x00
#define MAX_FRAME_LEN       256
#define MAX_CHANNELS        16
#define POLL_INTERVAL_MS    200
#define RECONNECT_DELAY_MS  2000

static int g_debug = 0;
#define DBG(fmt, ...) do { if (g_debug) printf(fmt, ##__VA_ARGS__); } while(0)

typedef enum {
    STATE_DISCONNECTED,
    STATE_SYNCING,
    STATE_ENABLING,
    STATE_POLLING,
} nv9_state_t;

typedef struct {
    int         fd;
    uint8_t     seq;
    nv9_state_t state;
    const char *dev;
    uint32_t    channel_value[MAX_CHANNELS];
    uint32_t    value_multiplier;
    char        currency[4];
    uint32_t    total_inserted;
} nv9_t;

typedef struct {
    uint8_t data[MAX_FRAME_LEN];
    int     len;
} frame_t;

static uint16_t crc16(const uint8_t *buf, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= (uint16_t)buf[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x8005)
                                 : (uint16_t)(crc << 1);
    }
    return crc;
}

static int build_frame(uint8_t seq, const uint8_t *payload, int payload_len,
                       uint8_t *out, int out_size)
{
    uint8_t inner[MAX_FRAME_LEN];
    int n = 0;

    inner[n++] = (uint8_t)((seq << 7) | SSP_ADDR);
    inner[n++] = (uint8_t)payload_len;
    for (int i = 0; i < payload_len; i++)
        inner[n++] = payload[i];

    uint16_t crc = crc16(inner, n);
    inner[n++] = (uint8_t)(crc & 0xFF);
    inner[n++] = (uint8_t)(crc >> 8);

    int out_len = 0;
    out[out_len++] = STX;
    for (int i = 0; i < n; i++) {
        if (out_len + 2 >= out_size) return -ENOSPC;
        out[out_len++] = inner[i];
        if (inner[i] == STX) out[out_len++] = STX;
    }
    return out_len;
}

static int read_response(int fd, uint8_t *buf, int buf_size, int timeout_ms)
{
    int total = 0;
    struct timespec deadline, now;

    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_nsec += (long)timeout_ms * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000L; }

    while (total < buf_size) {
        int n = (int)read(fd, buf + total, (size_t)(buf_size - total));
        if (n > 0) {
            total += n;
            if (total >= 4 && total >= 1 + 1 + 1 + buf[2] + 2) break;
        } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
            break;
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > deadline.tv_sec ||
            (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec))
            break;
        usleep(5000);
    }
    return total;
}

static int nv9_send(nv9_t *nv9, const char *name,
                    const uint8_t *cmd, int cmd_len, frame_t *resp)
{
    uint8_t frame[MAX_FRAME_LEN];
    int frame_len = build_frame(nv9->seq, cmd, cmd_len, frame, sizeof(frame));
    if (frame_len < 0) return -1;

    if (g_debug) {
        printf("[TX] %-12s:", name);
        for (int i = 0; i < frame_len; i++) printf(" %02X", frame[i]);
        printf("\n");
    }

    if (write(nv9->fd, frame, (size_t)frame_len) != frame_len) return -1;
    nv9->seq ^= 1;

    usleep(80000);
    int rlen = read_response(nv9->fd, resp->data, MAX_FRAME_LEN, 500);
    resp->len = rlen;

    if (g_debug) {
        printf("[RX] %-12s:", name);
        if (rlen > 0) { for (int i = 0; i < rlen; i++) printf(" %02X", resp->data[i]); }
        else printf(" (timeout)");
        printf("\n");
    }

    return rlen;
}

static int is_ok(const frame_t *resp)
{
    return resp->len >= 4 && resp->data[3] == RESP_OK;
}

/*
 * Parse SETUP_REQUEST response.
 * Layout (data starting at byte 4, after STX/seq/len/F0):
 *   [0]     unit_type
 *   [1]     fw_major
 *   [2]     fw_minor
 *   [3..5]  country_code (ASCII)
 *   [6..9]  value_multiplier (u32 LE)
 *   [10]    num_channels
 *   [11..]  channel values (4 bytes LE each)
 */
static void parse_setup_request(nv9_t *nv9, const frame_t *resp)
{
    if (!is_ok(resp) || resp->len < 16) return;

    const uint8_t *d = resp->data + 4;
    int avail = resp->len - 6; /* exclude STX+seq+len+F0 header and CRC */

    if (avail < 11) return;

    memcpy(nv9->currency, d + 3, 3);
    nv9->currency[3] = '\0';

    nv9->value_multiplier = (uint32_t)d[6]
                          | ((uint32_t)d[7] << 8)
                          | ((uint32_t)d[8] << 16)
                          | ((uint32_t)d[9] << 24);
    if (nv9->value_multiplier == 0)
        nv9->value_multiplier = 1;

    uint8_t num_ch = d[10];
    if (num_ch > MAX_CHANNELS) num_ch = MAX_CHANNELS;

    for (int i = 0; i < num_ch; i++) {
        int off = 11 + i * 4;
        if (off + 3 >= avail) break;
        uint32_t raw = (uint32_t)d[off]
                     | ((uint32_t)d[off+1] << 8)
                     | ((uint32_t)d[off+2] << 16)
                     | ((uint32_t)d[off+3] << 24);
        nv9->channel_value[i] = raw * nv9->value_multiplier;
    }

    printf("[*] FW: %u.%u | Currency: %s | Multiplier: %u\n",
           d[1], d[2], nv9->currency, nv9->value_multiplier);

    if (g_debug) {
        for (int i = 0; i < num_ch; i++) {
            if (nv9->channel_value[i])
                printf("[D]   ch%d = %u %s\n", i+1,
                       nv9->channel_value[i], nv9->currency);
        }
    }
}

static int serial_open(const char *dev)
{
    int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;

    int flags = fcntl(fd, F_GETFL, 0);
    if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0) { close(fd); return -1; }

    struct termios tty;
    if (tcgetattr(fd, &tty) < 0) { close(fd); return -1; }
    cfmakeraw(&tty);
    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);
    tty.c_cflag &= (tcflag_t)~(PARENB | CSTOPB | CSIZE);
    tty.c_cflag |= CS8 | CREAD | CLOCAL;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 10;
    if (tcsetattr(fd, TCSANOW, &tty) < 0) { close(fd); return -1; }

    int mbits = TIOCM_DTR | TIOCM_RTS;
    ioctl(fd, TIOCMBIS, &mbits);
    usleep(150000);
    tcflush(fd, TCIOFLUSH);
    return fd;
}

static int nv9_init_sequence(nv9_t *nv9)
{
    frame_t resp = {0};

    nv9->seq = 0;
    uint8_t cmd_sync[] = { CMD_SYNC };
    if (nv9_send(nv9, "SYNC", cmd_sync, 1, &resp) < 0 || !is_ok(&resp)) {
        printf("[!] SYNC failed\n"); return -1;
    }

    uint8_t cmd_setup[] = { CMD_SETUP_REQUEST };
    nv9_send(nv9, "SETUP_REQ", cmd_setup, 1, &resp);
    parse_setup_request(nv9, &resp);
    usleep(100000);

    uint8_t cmd_inh[] = { CMD_CH_INHIBITS, 0xFF, 0xFF };
    if (nv9_send(nv9, "CH_INHIBITS", cmd_inh, 3, &resp) < 0 || !is_ok(&resp)) {
        printf("[!] CH_INHIBITS failed\n"); return -1;
    }
    usleep(100000);

    uint8_t cmd_en[] = { CMD_ENABLE };
    if (nv9_send(nv9, "ENABLE", cmd_en, 1, &resp) < 0 || !is_ok(&resp)) {
        printf("[!] ENABLE failed\n"); return -1;
    }

    strncpy(nv9->currency, "VND", sizeof(nv9->currency));
    nv9->channel_value[0] =   1000;
    nv9->channel_value[1] =   2000;
    nv9->channel_value[2] =   5000;
    nv9->channel_value[3] =  10000;
    nv9->channel_value[4] =  20000;
    nv9->channel_value[5] =  50000;
    nv9->channel_value[6] = 100000;
    nv9->channel_value[7] = 200000;
    nv9->channel_value[8] = 500000;
    printf("[*] Ready — insert note\n\n");
    return 0;
}

static int parse_poll(nv9_t *nv9, const frame_t *resp)
{
    if (resp->len < 4) return -1;
    if (resp->data[3] != RESP_OK) { DBG("[D] POLL code: 0x%02X\n", resp->data[3]); return -1; }

    int payload_len = resp->data[2];
    if (payload_len <= 1) return 0;

    int need_reenable = 0;

    for (int i = 4; i < 4 + payload_len - 1 && i < resp->len - 2; i++) {
        uint8_t ev = resp->data[i];
        switch (ev) {
        case EVT_SLAVE_RESET:
            printf("[!] Validator reset\n");
            need_reenable = 1;
            break;
        case EVT_NOTE_READ:     DBG("[~] Reading note...\n");  break;
        case EVT_NOTE_STACKING: DBG("[~] Stacking...\n");      break;
        case EVT_NOTE_STACKED:  DBG("[~] Stacked\n");          break;
        case EVT_NOTE_CREDIT: {
            if (i + 1 >= resp->len - 2) break;
            uint8_t ch = resp->data[++i];
            uint32_t val = (ch >= 1 && ch <= MAX_CHANNELS)
                         ? nv9->channel_value[ch - 1] : 0;
            nv9->total_inserted += val;
            if (val)
                printf("[+] +%u %s   |   Total: %u %s\n",
                       val, nv9->currency, nv9->total_inserted, nv9->currency);
            else
                printf("[+] channel %u accepted   |   Total: %u %s\n",
                       ch, nv9->total_inserted, nv9->currency);
            break;
        }
        case EVT_NOTE_REJECTED:  printf("[-] Note rejected\n");          break;
        case EVT_NOTE_RETURNING: printf("[-] Note returning\n");         break;
        case EVT_DISABLED:       DBG("[D] Disabled\n");                  break;
        case EVT_INITIALIZING:   DBG("[D] Initializing\n");              break;
        case EVT_SAFE_JAM:       printf("[!] Safe jam\n");               break;
        case EVT_UNSAFE_JAM:     printf("[!] Unsafe jam\n");             break;
        case EVT_STACKER_FULL:   printf("[!] Stacker full\n");           break;
        default:                 DBG("[D] Unknown event: 0x%02X\n", ev); break;
        }
    }
    return need_reenable;
}

static void nv9_disconnect(nv9_t *nv9)
{
    if (nv9->fd >= 0) { close(nv9->fd); nv9->fd = -1; }
    nv9->state = STATE_DISCONNECTED;
}

int main(int argc, char *argv[])
{
    const char *dev = "/dev/ttyACM0";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) g_debug = 1;
        else dev = argv[i];
    }

    printf("NV9 SSP driver — %s%s\n", dev, g_debug ? " [DEBUG]" : "");
    printf("Ctrl+C to stop\n\n");

    nv9_t nv9;
    memset(&nv9, 0, sizeof(nv9));
    nv9.fd    = -1;
    nv9.state = STATE_DISCONNECTED;
    nv9.dev   = dev;

    frame_t    resp     = {0};
    uint8_t    cmd_poll[] = { CMD_POLL };

    while (1) {
        if (nv9.state == STATE_DISCONNECTED) {
            DBG("[*] Connecting to %s...\n", dev);
            nv9.fd = serial_open(dev);
            if (nv9.fd < 0) {
                printf("[!] Cannot open %s, retry in %ds\n",
                       dev, RECONNECT_DELAY_MS / 1000);
                usleep((useconds_t)(RECONNECT_DELAY_MS * 1000));
                continue;
            }
            nv9.state = STATE_SYNCING;
        }

        if (nv9.state == STATE_SYNCING || nv9.state == STATE_ENABLING) {
            if (nv9_init_sequence(&nv9) < 0) {
                printf("[!] Init failed, reconnecting...\n");
                nv9_disconnect(&nv9);
                usleep((useconds_t)(RECONNECT_DELAY_MS * 1000));
                continue;
            }
            nv9.state = STATE_POLLING;
        }

        memset(&resp, 0, sizeof(resp));
        if (nv9_send(&nv9, "POLL", cmd_poll, 1, &resp) <= 0) {
            printf("[!] Lost connection\n");
            nv9_disconnect(&nv9);
            usleep((useconds_t)(RECONNECT_DELAY_MS * 1000));
            continue;
        }

        int r = parse_poll(&nv9, &resp);
        if (r < 0) {
            printf("[!] POLL error, reconnecting...\n");
            nv9_disconnect(&nv9);
            usleep((useconds_t)(RECONNECT_DELAY_MS * 1000));
            continue;
        }
        if (r == 1) {
            nv9.state = STATE_ENABLING;
            usleep(500000);
            continue;
        }

        usleep((useconds_t)(POLL_INTERVAL_MS * 1000));
    }

    nv9_disconnect(&nv9);
    return 0;
}
