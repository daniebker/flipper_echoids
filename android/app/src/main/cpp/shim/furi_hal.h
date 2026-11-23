#pragma once

#include "furi.h"

uint32_t furi_hal_random_get(void);

/* The phone has no readable Bluetooth MAC: the app passes a stable per-device ID. */
const uint8_t* furi_hal_version_get_ble_mac(void);
const char* furi_hal_version_get_name_ptr(void);

/* No 5V header on a phone. */
bool furi_hal_power_is_otg_enabled(void);
bool furi_hal_power_enable_otg(void);
void furi_hal_power_disable_otg(void);

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t weekday;
} DateTime;

uint32_t furi_hal_rtc_get_timestamp(void);
void furi_hal_rtc_get_datetime(DateTime* datetime);
