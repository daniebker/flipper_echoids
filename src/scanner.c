#include "scanner.h"

#include <furi.h>
#include <furi_hal.h>
#include <expansion/expansion.h>
#include <string.h>

#define RX_BUF_SIZE      1024
#define LINE_MAX         160
#define BAUD_RATE        115200
#define WIFI_PHASE_MS    20000
#define BT_PHASE_MS      12000
#define DEMO_INTERVAL_MS 1800
#define DEMO_POOL        24

struct Scanner {
    bool running;
    ScanSource source;
    ScanUart uart;
    bool bt_enabled;

    /* UART */
    FuriHalSerialHandle* serial;
    FuriStreamBuffer* rx;
    Expansion* expansion;
    bool otg_enabled_by_us;
    char line[LINE_MAX];
    size_t line_len;
    bool alive;

    ScanPhase phase;
    uint32_t phase_started;
    uint32_t last_demo;
    const char* error;
};

static void scanner_rx_cb(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    Scanner* s = context;
    if(event & FuriHalSerialRxEventData) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(s->rx, &byte, 1, 0);
    }
}

static void scanner_send(Scanner* s, const char* cmd) {
    if(!s->serial) return;
    furi_hal_serial_tx(s->serial, (const uint8_t*)cmd, strlen(cmd));
    furi_hal_serial_tx_wait_complete(s->serial);
}

static void scanner_enter_phase(Scanner* s, ScanPhase phase) {
    s->phase = phase;
    s->phase_started = furi_get_tick();
    if(s->source != ScanSourceBoard) return;
    scanner_send(s, "stopscan\n");
    furi_delay_ms(50);
    if(phase == ScanPhaseWifi) scanner_send(s, "scanap\n");
    if(phase == ScanPhaseBt) scanner_send(s, "sniffbt\n");
}

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

bool scanner_start(Scanner* s, ScanSource source, ScanUart uart, bool bt_enabled) {
    if(s->running) scanner_stop(s);
    s->source = source;
    s->uart = uart;
    s->bt_enabled = bt_enabled;
    s->alive = false;
    s->error = NULL;
    s->line_len = 0;
    s->last_demo = furi_get_tick();
    furi_stream_buffer_reset(s->rx);

    if(source == ScanSourceBoard) {
        s->expansion = furi_record_open(RECORD_EXPANSION);
        expansion_disable(s->expansion);

        FuriHalSerialId id = uart == ScanUartLpuart ? FuriHalSerialIdLpuart :
                                                      FuriHalSerialIdUsart;
        s->serial = furi_hal_serial_control_acquire(id);
        if(!s->serial) {
            s->error = "UART busy";
            expansion_enable(s->expansion);
            furi_record_close(RECORD_EXPANSION);
            s->expansion = NULL;
            return false;
        }

        /* Most ESP32 boards are powered from the 5V pin. */
        if(!furi_hal_power_is_otg_enabled()) {
            s->otg_enabled_by_us = furi_hal_power_enable_otg();
            furi_delay_ms(300); /* let the board boot */
        }

        furi_hal_serial_init(s->serial, BAUD_RATE);
        furi_hal_serial_async_rx_start(s->serial, scanner_rx_cb, s, false);
    }

    s->running = true;
    scanner_enter_phase(s, ScanPhaseWifi);
    return true;
}

void scanner_stop(Scanner* s) {
    if(!s->running) return;
    if(s->serial) {
        scanner_send(s, "stopscan\n");
        furi_hal_serial_async_rx_stop(s->serial);
        furi_hal_serial_deinit(s->serial);
        furi_hal_serial_control_release(s->serial);
        s->serial = NULL;
    }
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
    return s->source == ScanSourceDemo ? s->running : s->alive;
}

const char* scanner_error(Scanner* s) {
    return s->error;
}

static void scanner_update_phase(Scanner* s) {
    uint32_t elapsed = furi_get_tick() - s->phase_started;
    if(s->phase == ScanPhaseWifi && elapsed > furi_ms_to_ticks(WIFI_PHASE_MS)) {
        if(s->bt_enabled)
            scanner_enter_phase(s, ScanPhaseBt);
        else
            s->phase_started = furi_get_tick();
    } else if(s->phase == ScanPhaseBt && elapsed > furi_ms_to_ticks(BT_PHASE_MS)) {
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
    bool bt = s->bt_enabled && s->phase == ScanPhaseBt;
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
    } else {
        snprintf(
            out->name,
            SCAN_NAME_LEN,
            "%s",
            demo_names[idx % (sizeof(demo_names) / sizeof(demo_names[0]))]);
    }
    return true;
}

bool scanner_poll(Scanner* s, ScanEvent* out) {
    if(!s->running) return false;
    scanner_update_phase(s);

    if(s->source == ScanSourceDemo) return scanner_demo(s, out);

    uint8_t byte;
    while(furi_stream_buffer_receive(s->rx, &byte, 1, 0) == 1) {
        s->alive = true;
        if(byte == '\n' || byte == '\r') {
            if(s->line_len == 0) continue;
            s->line[s->line_len] = '\0';
            s->line_len = 0;
            if(scanner_parse_line(s->line, s->phase, out)) return true;
        } else if(s->line_len < LINE_MAX - 1) {
            s->line[s->line_len++] = (char)byte;
        }
    }
    return false;
}
