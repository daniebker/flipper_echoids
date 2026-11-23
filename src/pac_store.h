/*
 * PAC storage: echoids that aren't in the team live in a file on the SD card,
 * so the number of PACs (and stored echoids) is only limited by card space.
 * The file is a flat array of Echoid records; the count is kept in the save.
 */
#pragma once

#include "game.h"

#define PAC_NOT_FOUND UINT32_MAX

/* Number of whole records in the file (used to sanity-check the saved count). */
uint32_t pac_file_count(void);
bool pac_append(const Echoid* e);
bool pac_read(uint32_t idx, Echoid* out);
bool pac_write(uint32_t idx, const Echoid* e);
/* Reads up to n records starting at start; returns how many were read. */
uint8_t pac_read_page(uint32_t start, Echoid* out, uint8_t n);
/* Removes record idx (of count), keeping the order of the rest. */
bool pac_remove(uint32_t idx, uint32_t count);
/* Index of the stored echoid with this MAC, or PAC_NOT_FOUND. */
uint32_t pac_find(const uint8_t mac[6], uint32_t count);
void pac_clear(void);
