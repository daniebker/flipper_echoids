/*
 * Scanner for the phone build: implements scanner.h using the phone's own
 * WiFi and Bluetooth radios. The Java side scans and pushes sightings in with
 * scanner_phone_push(); the game polls them exactly as it would from an ESP32.
 * Demo mode works as on the Flipper.
 */
#include <pthread.h>

#include "scanner.h"
#include "scanner_phone.h"

#include <furi.h>
#include <furi_hal.h>

#define QUEUE_MAX        64
#define DEMO_INTERVAL_MS 1800
#define DEMO_POOL        24

const uint32_t scanner_baud_rates[] = {115200};
const size_t scanner_baud_count = 1;
const uint16_t scanner_boot_waits_ms[] = {0};
const size_t scanner_boot_count = 1;
const char* const scanner_wifi_cmds[] = {"phone"};
const size_t scanner_wifi_cmd_count = 1;
const char* const scanner_bt_cmds[] = {"phone"};
const size_t scanner_bt_cmd_count = 1;

const char* scanner_uart_name(uint8_t uart) {
    UNUSED(uart);
    return "phone radios";
}

const char* scanner_power_name(uint8_t power) {
    UNUSED(power);
    return "n/a";
}

struct Scanner {
    bool running;
    ScanSource source;
    ScanConfig cfg;
    ScanPhase phase;
    uint32_t last_demo;
    ScanStats stats;
};

/* Shared with the Java side. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static ScanEvent queue[QUEUE_MAX];
static int q_head, q_count;
static volatile int wanted; /* 0 none, 1 radios, 2 radios incl. Bluetooth */
static char phone_error[48];

void scanner_phone_push(const ScanEvent* ev) {
    pthread_mutex_lock(&lock);
    if(wanted) {
        if(q_count == QUEUE_MAX) { /* drop the oldest */
            q_head = (q_head + 1) % QUEUE_MAX;
            q_count--;
        }
        queue[(q_head + q_count) % QUEUE_MAX] = *ev;
        q_count++;
    }
    pthread_mutex_unlock(&lock);
}

int scanner_phone_wanted(void) {
    return wanted;
}

void scanner_phone_set_error(const char* error) {
    pthread_mutex_lock(&lock);
    if(error) {
        strncpy(phone_error, error, sizeof(phone_error) - 1);
        phone_error[sizeof(phone_error) - 1] = '\0';
    } else {
        phone_error[0] = '\0';
    }
    pthread_mutex_unlock(&lock);
}

Scanner* scanner_alloc(void) {
    return calloc(1, sizeof(Scanner));
}

void scanner_free(Scanner* s) {
    scanner_stop(s);
    free(s);
}

bool scanner_start(Scanner* s, ScanSource source, const ScanConfig* cfg) {
    s->running = true;
    s->source = source;
    s->cfg = *cfg;
    s->phase = ScanPhaseWifi;
    s->last_demo = furi_get_tick();
    memset(&s->stats, 0, sizeof(s->stats));
    pthread_mutex_lock(&lock);
    q_count = 0;
    wanted = source == ScanSourceBoard ? (cfg->bt_enabled ? 2 : 1) : 0;
    pthread_mutex_unlock(&lock);
    return true;
}

bool scanner_start_manual(Scanner* s, const ScanConfig* cfg) {
    UNUSED(s);
    UNUSED(cfg);
    return false; /* no UART on a phone */
}

void scanner_stop(Scanner* s) {
    s->running = false;
    s->phase = ScanPhaseIdle;
    pthread_mutex_lock(&lock);
    wanted = 0;
    q_count = 0;
    pthread_mutex_unlock(&lock);
}

bool scanner_is_running(Scanner* s) {
    return s->running;
}

void scanner_send_cmd(Scanner* s, const char* cmd) {
    UNUSED(s);
    UNUSED(cmd);
}

static const char* const demo_names[] = {
    "HomeNet",  "CoffeeBar", "FreeWiFi", "Linksys", "NETGEAR42", "Office-5G",
    "Pixel_7",  "TP-Link",   "Guest",    "Library", "Airport",   "Hotel_Lobby",
};

static bool scanner_demo(Scanner* s, ScanEvent* out) {
    uint32_t now = furi_get_tick();
    if(now - s->last_demo < DEMO_INTERVAL_MS) return false;
    s->last_demo = now;
    memset(out, 0, sizeof(*out));
    uint32_t idx = furi_hal_random_get() % DEMO_POOL;
    bool bt = s->cfg.bt_enabled && (furi_hal_random_get() % 3 == 0);
    out->type = bt ? ScanEventBt : ScanEventAp;
    out->mac[0] = bt ? 0xD0 : 0xDE;
    out->mac[1] = 0x00;
    out->mac[2] = 0x5E;
    out->mac[3] = (uint8_t)(idx * 37);
    out->mac[4] = (uint8_t)(idx * 101 + 7);
    out->mac[5] = (uint8_t)idx;
    out->rssi = (int8_t)(-40 - (int)(furi_hal_random_get() % 50));
    if(bt)
        snprintf(out->name, SCAN_NAME_LEN, "BLE-%02X%02X", out->mac[4], out->mac[5]);
    else
        snprintf(out->name, SCAN_NAME_LEN, "%s", demo_names[idx % 12]);
    s->phase = bt ? ScanPhaseBt : ScanPhaseWifi;
    return true;
}

bool scanner_poll(Scanner* s, ScanEvent* out) {
    if(!s->running) return false;
    bool got = false;
    if(s->source == ScanSourceDemo) {
        got = scanner_demo(s, out);
    } else {
        pthread_mutex_lock(&lock);
        if(q_count) {
            *out = queue[q_head];
            q_head = (q_head + 1) % QUEUE_MAX;
            q_count--;
            got = true;
        }
        pthread_mutex_unlock(&lock);
        if(got) s->phase = out->type == ScanEventBt ? ScanPhaseBt : ScanPhaseWifi;
    }
    if(got) {
        if(out->type == ScanEventAp)
            s->stats.aps++;
        else
            s->stats.bts++;
        s->stats.lines++;
    }
    return got;
}

ScanPhase scanner_phase(Scanner* s) {
    return s->phase;
}

bool scanner_board_alive(Scanner* s) {
    return s->running;
}

bool scanner_booting(Scanner* s) {
    UNUSED(s);
    return false;
}

const char* scanner_error(Scanner* s) {
    if(!s->running || s->source != ScanSourceBoard) return NULL;
    return phone_error[0] ? phone_error : NULL;
}

void scanner_get_stats(Scanner* s, ScanStats* out) {
    *out = s->stats;
}

void scanner_reset_stats(Scanner* s) {
    memset(&s->stats, 0, sizeof(s->stats));
}

size_t scanner_get_log(Scanner* s, char out[SCAN_LOG_LINES][SCAN_LOG_LEN]) {
    UNUSED(s);
    UNUSED(out);
    return 0;
}

void scanner_last_rx(Scanner* s, char* out, size_t size) {
    UNUSED(s);
    if(size) out[0] = '\0';
}

bool scanner_power_on(void) {
    return false;
}

void scanner_power_off(void) {
}
