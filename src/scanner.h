/*
 * Scanner: talks to an ESP32 board (e.g. the official WiFi Devboard) running
 * ESP32 Marauder over UART, or produces simulated sightings in demo mode.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCAN_NAME_LEN  18
#define SCAN_LOG_LINES 6
#define SCAN_LOG_LEN   32

typedef enum {
    ScanSourceBoard,
    ScanSourceDemo,
} ScanSource;

typedef enum {
    ScanUartUsart, /* pins 13 (TX) / 14 (RX) - WiFi Devboard default */
    ScanUartLpuart, /* pins 15 (TX) / 16 (RX) */
    ScanUartCount,
} ScanUart;

typedef enum {
    ScanPowerAuto, /* turn 5V on while scanning, off afterwards */
    ScanPowerKeepOn, /* turn 5V on and leave it on */
    ScanPowerUntouched, /* never touch 5V (board powered by USB etc.) */
    ScanPowerCount,
} ScanPower;

typedef enum {
    ScanEventAp,
    ScanEventBt,
} ScanEventType;

typedef struct {
    uint8_t type;
    uint8_t mac[6];
    int8_t rssi;
    char name[SCAN_NAME_LEN];
} ScanEvent;

typedef enum {
    ScanPhaseIdle,
    ScanPhaseWifi,
    ScanPhaseBt,
} ScanPhase;

/* All values are indexes so that a zeroed config is the default one. */
typedef struct {
    uint8_t uart; /* ScanUart */
    uint8_t baud_idx; /* scanner_baud_rates[] */
    uint8_t power; /* ScanPower */
    uint8_t boot_idx; /* scanner_boot_waits_ms[] */
    uint8_t eol; /* 0 = "\n", 1 = "\r\n" */
    uint8_t wifi_cmd; /* scanner_wifi_cmds[] */
    uint8_t bt_cmd; /* scanner_bt_cmds[] */
    uint8_t bt_enabled;
} ScanConfig;

typedef struct {
    uint32_t rx_bytes;
    uint32_t rx_printable;
    uint32_t rx_garbage;
    uint32_t lines;
    uint32_t aps;
    uint32_t bts;
    uint32_t tx_cmds;
    uint32_t resends;
} ScanStats;

extern const uint32_t scanner_baud_rates[];
extern const size_t scanner_baud_count;
extern const uint16_t scanner_boot_waits_ms[];
extern const size_t scanner_boot_count;
extern const char* const scanner_wifi_cmds[];
extern const size_t scanner_wifi_cmd_count;
extern const char* const scanner_bt_cmds[];
extern const size_t scanner_bt_cmd_count;

const char* scanner_uart_name(uint8_t uart);
const char* scanner_power_name(uint8_t power);

typedef struct Scanner Scanner;

Scanner* scanner_alloc(void);
void scanner_free(Scanner* s);

/* Normal game scanning: cycles WiFi / BT scans automatically. */
bool scanner_start(Scanner* s, ScanSource source, const ScanConfig* cfg);
/* Debug terminal: opens the UART but only sends what you tell it to. */
bool scanner_start_manual(Scanner* s, const ScanConfig* cfg);
void scanner_stop(Scanner* s);
bool scanner_is_running(Scanner* s);

/* Sends a command (line ending from the config is appended). */
void scanner_send_cmd(Scanner* s, const char* cmd);

/* Call regularly from the app's main loop. Returns true while an event was produced. */
bool scanner_poll(Scanner* s, ScanEvent* out);

ScanPhase scanner_phase(Scanner* s);
/* True once any data at all has been received from the board. */
bool scanner_board_alive(Scanner* s);
/* True while waiting for the board to boot after powering it. */
bool scanner_booting(Scanner* s);
const char* scanner_error(Scanner* s);
void scanner_get_stats(Scanner* s, ScanStats* out);
void scanner_reset_stats(Scanner* s);
/* Copies the raw RX/TX log, oldest first. Returns number of lines. */
size_t scanner_get_log(Scanner* s, char out[SCAN_LOG_LINES][SCAN_LOG_LEN]);

/* 5V helpers for calibration, which keeps the board powered across retries. */
bool scanner_power_on(void); /* returns true if it was turned on by this call */
void scanner_power_off(void);

/* Exposed for testing: parse one line of Marauder (or simple CSV) output. */
bool scanner_parse_line(const char* line, ScanPhase phase, ScanEvent* out);
/* If buf (NUL terminated, len chars) ends with the start of a second
 * "<rssi> Device: " record, returns where the first record ends, else 0. */
size_t scanner_record_split(const char* buf, size_t len);
