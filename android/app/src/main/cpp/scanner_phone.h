/* Bridge between the phone's radios (Java) and the game's scanner. */
#pragma once

#include "scanner.h"

/* Adds a WiFi or Bluetooth sighting (ignored unless the game is scanning). */
void scanner_phone_push(const ScanEvent* ev);
/* 0 = not scanning, 1 = scan WiFi, 2 = scan WiFi and Bluetooth. */
int scanner_phone_wanted(void);
/* Shown in Collect when scanning can't work (permissions, radios off); NULL clears it. */
void scanner_phone_set_error(const char* error);
