/*
 * Scanner: talks to an ESP32 board (e.g. the official WiFi Devboard) running
 * ESP32 Marauder over UART, or produces simulated sightings in demo mode.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SCAN_NAME_LEN 18

typedef enum {
    ScanSourceBoard,
    ScanSourceDemo,
} ScanSource;

typedef enum {
    ScanUartUsart, /* pins 13 (TX) / 14 (RX) - WiFi Devboard default */
    ScanUartLpuart, /* pins 15 (TX) / 16 (RX) */
} ScanUart;

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

typedef struct Scanner Scanner;

Scanner* scanner_alloc(void);
void scanner_free(Scanner* s);

bool scanner_start(Scanner* s, ScanSource source, ScanUart uart, bool bt_enabled);
void scanner_stop(Scanner* s);
bool scanner_is_running(Scanner* s);

/* Call regularly from the app's main loop. Returns true while an event was produced. */
bool scanner_poll(Scanner* s, ScanEvent* out);

ScanPhase scanner_phase(Scanner* s);
/* True once any data at all has been received from the board. */
bool scanner_board_alive(Scanner* s);
const char* scanner_error(Scanner* s);

/* Exposed for testing: parse one line of Marauder (or simple CSV) output. */
bool scanner_parse_line(const char* line, ScanPhase phase, ScanEvent* out);
