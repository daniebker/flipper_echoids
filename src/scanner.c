#include "scanner.h"

#include <furi.h>
#include <furi_hal.h>
#include <expansion/expansion.h>
#include <storage/storage.h>
#include <stdio.h>
#include <string.h>

#define RX_BUF_SIZE      2048
#define LINE_MAX         192
#define WIFI_PHASE_MS    20000
#define BT_PHASE_MS      12000
#define RESEND_MS        4000
#define PARTIAL_FLUSH_MS 400
#define DEMO_INTERVAL_MS 1800
#define DEMO_POOL        24

const uint32_t scanner_baud_rates[] = {115200, 230400, 460800, 921600, 57600, 38400, 9600};
const size_t scanner_baud_count = sizeof(scanner_baud_rates) / sizeof(scanner_baud_rates[0]);
const uint16_t scanner_boot_waits_ms[] = {2000, 3000, 5000, 0, 500, 1000};
const size_t scanner_boot_count = sizeof(scanner_boot_waits_ms) / sizeof(scanner_boot_waits_ms[0]);
/* Current Marauder only has "scanall"; "scanap" is for older Marauder firmware. */
const char* const scanner_wifi_cmds[] = {"scanall", "scanap"};
const size_t scanner_wifi_cmd_count = sizeof(scanner_wifi_cmds) / sizeof(scanner_wifi_cmds[0]);
const char* const scanner_bt_cmds[] = {"sniffbt"};
const size_t scanner_bt_cmd_count = sizeof(scanner_bt_cmds) / sizeof(scanner_bt_cmds[0]);

const char* scanner_uart_name(uint8_t uart) {
    return uart == ScanUartLpuart ? "LPUART 15/16" : "USART 13/14";
}

const char* scanner_power_name(uint8_t power) {
    switch(power) {
    case ScanPowerKeepOn:
        return "Keep on";
    case ScanPowerUntouched:
        return "Don't touch";
    default:
        return "Auto";
    }
}

struct Scanner {
    bool running;
    bool manual;
    ScanSource source;
    ScanConfig cfg;

    /* UART */
    FuriHalSerialHandle* serial;
    FuriStreamBuffer* rx;
    Expansion* expansion;
    bool otg_enabled_by_us;
    char line[LINE_MAX];
    size_t line_len;
    uint32_t last_rx_tick;

    /* Scan cycle */
    ScanPhase phase;
    bool cmd_pending;
    uint32_t ready_tick;
    uint32_t phase_started;
    uint32_t last_cmd_tick;
    uint32_t bytes_at_cmd;
    uint32_t last_demo;
    const char* error;
    bool bt_unsupported;

    ScanStats stats;
    char last_rx[SCAN_LOG_LEN * 2];
    Storage* storage;
    File* log_file;
    char log[SCAN_LOG_LINES][SCAN_LOG_LEN];
    uint8_t log_pos;
    uint8_t log_count;
};

/* ------------------------------------------------------------------------- */

/* Raw traffic log on the SD card, for debugging board output formats. */
static void scanner_file_open(Scanner* s) {
    s->storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(s->storage, "/ext/apps_data/echoids");
    s->log_file = storage_file_alloc(s->storage);
    if(!storage_file_open(s->log_file, SCAN_LOG_PATH, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        storage_file_free(s->log_file);
        s->log_file = NULL;
        furi_record_close(RECORD_STORAGE);
        s->storage = NULL;
    }
}

static void scanner_file_close(Scanner* s) {
    if(s->log_file) {
        storage_file_close(s->log_file);
        storage_file_free(s->log_file);
        s->log_file = NULL;
    }
    if(s->storage) {
        furi_record_close(RECORD_STORAGE);
        s->storage = NULL;
    }
}

static void scanner_file_write(Scanner* s, const char* prefix, const char* text, size_t len) {
    if(!s->log_file) return;
    if(storage_file_size(s->log_file) > SCAN_LOG_MAX_SIZE) return;
    storage_file_write(s->log_file, prefix, strlen(prefix));
    storage_file_write(s->log_file, text, len);
    storage_file_write(s->log_file, "\n", 1);
}

void scanner_last_rx(Scanner* s, char* out, size_t size) {
    strncpy(out, s->last_rx, size - 1);
    out[size - 1] = '\0';
}

static void scanner_log(Scanner* s, char prefix, const char* text, size_t len) {
    char* dst = s->log[s->log_pos];
    size_t n = 0;
    dst[n++] = prefix;
    for(size_t i = 0; i < len && n < SCAN_LOG_LEN - 1; i++) {
        unsigned char c = (unsigned char)text[i];
        dst[n++] = (c >= 0x20 && c < 0x7F) ? (char)c : '.';
    }
    dst[n] = '\0';
    s->log_pos = (s->log_pos + 1) % SCAN_LOG_LINES;
    if(s->log_count < SCAN_LOG_LINES) s->log_count++;
}

size_t scanner_get_log(Scanner* s, char out[SCAN_LOG_LINES][SCAN_LOG_LEN]) {
    uint8_t start = (uint8_t)((s->log_pos + SCAN_LOG_LINES - s->log_count) % SCAN_LOG_LINES);
    for(uint8_t i = 0; i < s->log_count; i++) {
        memcpy(out[i], s->log[(start + i) % SCAN_LOG_LINES], SCAN_LOG_LEN);
    }
    return s->log_count;
}

static void scanner_rx_cb(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    Scanner* s = context;
    if(event & FuriHalSerialRxEventData) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(s->rx, &byte, 1, 0);
    }
}

static void scanner_tx_raw(Scanner* s, const char* cmd) {
    if(!s->serial) return;
    furi_hal_serial_tx(s->serial, (const uint8_t*)cmd, strlen(cmd));
    const char* eol = s->cfg.eol ? "\r\n" : "\n";
    furi_hal_serial_tx(s->serial, (const uint8_t*)eol, strlen(eol));
    furi_hal_serial_tx_wait_complete(s->serial);
    s->stats.tx_cmds++;
    scanner_log(s, '>', cmd, strlen(cmd));
    scanner_file_write(s, "TX> ", cmd, strlen(cmd));
}

void scanner_send_cmd(Scanner* s, const char* cmd) {
    if(!s->running || s->source != ScanSourceBoard) return;
    if(s->manual) {
        /* Track what the board is doing so the parser knows how to read lines. */
        if(strncmp(cmd, "stopscan", 8) == 0)
            s->phase = ScanPhaseIdle;
        else if(strncmp(cmd, "scanap", 6) == 0 || strncmp(cmd, "scanall", 7) == 0)
            s->phase = ScanPhaseWifi;
        else if(strncmp(cmd, "sniffbt", 7) == 0)
            s->phase = ScanPhaseBt;
    }
    scanner_tx_raw(s, cmd);
}

static void scanner_enter_phase(Scanner* s, ScanPhase phase) {
    s->phase = phase;
    s->phase_started = furi_get_tick();
    s->cmd_pending = s->source == ScanSourceBoard;
}

static void scanner_send_phase_cmd(Scanner* s) {
    s->cmd_pending = false;
    scanner_tx_raw(s, "stopscan");
    furi_delay_ms(50);
    if(s->phase == ScanPhaseWifi)
        scanner_tx_raw(s, scanner_wifi_cmds[s->cfg.wifi_cmd % scanner_wifi_cmd_count]);
    if(s->phase == ScanPhaseBt)
        scanner_tx_raw(s, scanner_bt_cmds[s->cfg.bt_cmd % scanner_bt_cmd_count]);
    s->last_cmd_tick = furi_get_tick();
    s->phase_started = s->last_cmd_tick;
    s->bytes_at_cmd = s->stats.rx_bytes;
}

/* ------------------------------------------------------------------------- */

Scanner* scanner_alloc(void) {
    Scanner* s = malloc(sizeof(Scanner));
    memset(s, 0, sizeof(Scanner));
    s->rx = furi_stream_buffer_alloc(RX_BUF_SIZE, 1);
    return s;
}

void scanner_free(Scanner* s) {
    scanner_stop(s);
    furi_stream_buffer_free(s->rx);
    free(s);
}

bool scanner_power_on(void) {
    if(furi_hal_power_is_otg_enabled()) return false;
    return furi_hal_power_enable_otg();
}

void scanner_power_off(void) {
    furi_hal_power_disable_otg();
}

static bool scanner_open(Scanner* s, ScanSource source, const ScanConfig* cfg, bool manual) {
    if(s->running) scanner_stop(s);
    s->source = source;
    s->cfg = *cfg;
    s->manual = manual;
    s->error = NULL;
    s->line_len = 0;
    s->cmd_pending = false;
    s->phase = ScanPhaseIdle;
    s->last_demo = furi_get_tick();
    s->ready_tick = furi_get_tick();
    s->bt_unsupported = false;
    s->last_rx[0] = '\0';
    memset(&s->stats, 0, sizeof(s->stats));
    furi_stream_buffer_reset(s->rx);

    if(source == ScanSourceBoard) {
        uint32_t baud = scanner_baud_rates[cfg->baud_idx % scanner_baud_count];
        s->expansion = furi_record_open(RECORD_EXPANSION);
        expansion_disable(s->expansion);

        FuriHalSerialId id = cfg->uart == ScanUartLpuart ? FuriHalSerialIdLpuart :
                                                           FuriHalSerialIdUsart;
        s->serial = furi_hal_serial_control_acquire(id);
        if(!s->serial) {
            s->error = "UART busy (close other apps)";
        } else if(!furi_hal_serial_is_baud_rate_supported(s->serial, baud)) {
            s->error = "Baud not supported";
            furi_hal_serial_control_release(s->serial);
            s->serial = NULL;
        }
        if(!s->serial) {
            expansion_enable(s->expansion);
            furi_record_close(RECORD_EXPANSION);
            s->expansion = NULL;
            return false;
        }

        if(cfg->log_sd) {
            scanner_file_open(s);
            char head[64];
            snprintf(
                head,
                sizeof(head),
                "%s @%lu %s",
                scanner_uart_name(cfg->uart),
                (unsigned long)baud,
                manual ? "(terminal)" : "(collect)");
            scanner_file_write(s, "==== ", head, strlen(head));
        }

        furi_hal_serial_init(s->serial, baud);
        furi_hal_serial_async_rx_start(s->serial, scanner_rx_cb, s, false);

        char msg[SCAN_LOG_LEN];
        snprintf(msg, sizeof(msg), "%s @%lu", scanner_uart_name(cfg->uart), (unsigned long)baud);
        scanner_log(s, '#', msg, strlen(msg));

        if(cfg->power != ScanPowerUntouched) {
            bool turned_on = scanner_power_on();
            if(turned_on) {
                /* The ESP32 needs time to boot before it accepts commands. */
                s->ready_tick =
                    furi_get_tick() +
                    furi_ms_to_ticks(scanner_boot_waits_ms[cfg->boot_idx % scanner_boot_count]);
                s->otg_enabled_by_us = cfg->power == ScanPowerAuto;
                scanner_log(s, '#', "5V on, board booting", 20);
            } else if(!furi_hal_power_is_otg_enabled()) {
                scanner_log(s, '#', "5V enable FAILED", 16);
            }
        }
    }

    s->running = true;
    return true;
}

bool scanner_start(Scanner* s, ScanSource source, const ScanConfig* cfg) {
    if(!scanner_open(s, source, cfg, false)) return false;
    scanner_enter_phase(s, ScanPhaseWifi);
    return true;
}

bool scanner_start_manual(Scanner* s, const ScanConfig* cfg) {
    return scanner_open(s, ScanSourceBoard, cfg, true);
}

void scanner_stop(Scanner* s) {
    if(!s->running) return;
    if(s->serial) {
        if(!s->manual) scanner_tx_raw(s, "stopscan");
        furi_hal_serial_async_rx_stop(s->serial);
        furi_hal_serial_deinit(s->serial);
        furi_hal_serial_control_release(s->serial);
        s->serial = NULL;
    }
    scanner_file_close(s);
    if(s->otg_enabled_by_us) {
        furi_hal_power_disable_otg();
        s->otg_enabled_by_us = false;
    }
    if(s->expansion) {
        expansion_enable(s->expansion);
        furi_record_close(RECORD_EXPANSION);
        s->expansion = NULL;
    }
    s->running = false;
    s->phase = ScanPhaseIdle;
}

bool scanner_is_running(Scanner* s) {
    return s->running;
}

ScanPhase scanner_phase(Scanner* s) {
    return s->phase;
}

bool scanner_board_alive(Scanner* s) {
    return s->source == ScanSourceDemo ? s->running : s->stats.rx_printable > 0;
}

bool scanner_booting(Scanner* s) {
    return s->running && s->source == ScanSourceBoard &&
           (int32_t)(furi_get_tick() - s->ready_tick) < 0;
}

const char* scanner_error(Scanner* s) {
    return s->error;
}

void scanner_get_stats(Scanner* s, ScanStats* out) {
    *out = s->stats;
}

void scanner_reset_stats(Scanner* s) {
    memset(&s->stats, 0, sizeof(s->stats));
    s->bytes_at_cmd = 0;
}

/* ------------------------------------------------------------------------- */

static void scanner_update_phase(Scanner* s) {
    if(s->manual || s->phase == ScanPhaseIdle) return;
    uint32_t now = furi_get_tick();

    if(s->cmd_pending) {
        if(s->source == ScanSourceBoard && (int32_t)(now - s->ready_tick) >= 0)
            scanner_send_phase_cmd(s);
        return;
    }

    /* Board said nothing at all since our command: it probably missed it. */
    if(s->source == ScanSourceBoard && s->stats.rx_bytes == s->bytes_at_cmd &&
       now - s->last_cmd_tick > furi_ms_to_ticks(RESEND_MS)) {
        s->stats.resends++;
        scanner_send_phase_cmd(s);
        return;
    }

    uint32_t elapsed = now - s->phase_started;
    if(s->phase == ScanPhaseWifi && elapsed > furi_ms_to_ticks(WIFI_PHASE_MS)) {
        if(s->cfg.bt_enabled && !s->bt_unsupported)
            scanner_enter_phase(s, ScanPhaseBt);
        else
            s->phase_started = now;
    } else if(
        s->phase == ScanPhaseBt &&
        (elapsed > furi_ms_to_ticks(BT_PHASE_MS) || s->bt_unsupported)) {
        scanner_enter_phase(s, ScanPhaseWifi);
    }
}

static const char* const demo_names[] = {
    "HomeNet",  "CoffeeBar", "FreeWiFi", "Linksys", "NETGEAR42", "Office-5G",
    "Pixel_7",  "TP-Link",   "Guest",    "Library", "Airport",   "Hotel_Lobby",
};

static bool scanner_demo(Scanner* s, ScanEvent* out) {
    uint32_t now = furi_get_tick();
    if(now - s->last_demo < furi_ms_to_ticks(DEMO_INTERVAL_MS)) return false;
    s->last_demo = now;
    memset(out, 0, sizeof(*out));

    /* A fixed pool of fake devices so they reappear (and stay the same). */
    uint32_t idx = furi_hal_random_get() % DEMO_POOL;
    bool bt = s->cfg.bt_enabled && s->phase == ScanPhaseBt;
    out->type = bt ? ScanEventBt : ScanEventAp;
    out->mac[0] = bt ? 0xD0 : 0xDE;
    out->mac[1] = 0x00;
    out->mac[2] = 0x5E;
    out->mac[3] = (uint8_t)(idx * 37);
    out->mac[4] = (uint8_t)(idx * 101 + 7);
    out->mac[5] = (uint8_t)idx;
    out->rssi = (int8_t)(-40 - (int)(furi_hal_random_get() % 50));
    if(bt) {
        snprintf(out->name, SCAN_NAME_LEN, "BLE-%02X%02X", out->mac[4], out->mac[5]);
        s->stats.bts++;
    } else {
        snprintf(
            out->name,
            SCAN_NAME_LEN,
            "%s",
            demo_names[idx % (sizeof(demo_names) / sizeof(demo_names[0]))]);
        s->stats.aps++;
    }
    return true;
}

static bool scanner_handle_record(Scanner* s, char* rec, size_t len, ScanEvent* out) {
    rec[len] = '\0';
    s->stats.lines++;
    scanner_log(s, ' ', rec, len);
    scanner_file_write(s, "", rec, len);
    strncpy(s->last_rx, rec, sizeof(s->last_rx) - 1);
    s->last_rx[sizeof(s->last_rx) - 1] = '\0';
    if(strstr(rec, "Bluetooth not supported")) {
        s->bt_unsupported = true;
        scanner_log(s, '#', "Board has no BT: skipping", 25);
    }
    if(!scanner_parse_line(rec, s->phase, out)) return false;
    if(out->type == ScanEventAp)
        s->stats.aps++;
    else
        s->stats.bts++;
    return true;
}

static bool scanner_finish_line(Scanner* s, ScanEvent* out) {
    size_t len = s->line_len;
    s->line_len = 0;
    return scanner_handle_record(s, s->line, len, out);
}

/* Marauder builds without a screen print Bluetooth sightings as
 * "-71 Device: name" with no newline, so records run together:
 * "-71 Device: Foo-65 Device: Bar". When a second "Device: " arrives,
 * cut the buffer just before that record's RSSI. */
static bool scanner_split_bt(Scanner* s, ScanEvent* out) {
    size_t len = s->line_len;
    s->line[len] = '\0';
    size_t cut = scanner_record_split(s->line, len);
    if(cut == 0) return false;

    char rec[LINE_MAX];
    memcpy(rec, s->line, cut);
    size_t rest = len - cut;
    memmove(s->line, s->line + cut, rest);
    s->line_len = rest;
    return scanner_handle_record(s, rec, cut, out);
}

bool scanner_poll(Scanner* s, ScanEvent* out) {
    if(!s->running) return false;
    scanner_update_phase(s);

    if(s->source == ScanSourceDemo) return scanner_demo(s, out);

    uint8_t byte;
    while(furi_stream_buffer_receive(s->rx, &byte, 1, 0) == 1) {
        s->stats.rx_bytes++;
        s->last_rx_tick = furi_get_tick();
        if(byte == '\n' || byte == '\r') {
            if(s->line_len == 0) continue;
            if(scanner_finish_line(s, out)) return true;
            continue;
        }
        if((byte >= 0x20 && byte < 0x7F) || byte == '\t')
            s->stats.rx_printable++;
        else
            s->stats.rx_garbage++;
        s->line[s->line_len++] = (char)byte;
        if(byte == ' ' && scanner_split_bt(s, out)) return true;
        if(s->line_len >= LINE_MAX - 1) {
            if(scanner_finish_line(s, out)) return true;
        }
    }

    /* Show prompts / garbage that never ends with a newline. */
    if(s->line_len && furi_get_tick() - s->last_rx_tick > furi_ms_to_ticks(PARTIAL_FLUSH_MS)) {
        if(scanner_finish_line(s, out)) return true;
    }
    return false;
}
