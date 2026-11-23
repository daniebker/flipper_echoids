#pragma once

#include "game.h"
#include "scanner.h"

#define SAVE_MAGIC   0x45434831u /* "ECH1" */
#define SAVE_VERSION 7 /* v7: 16 item slots (v6: PAC echoids in pac.bin) */
#define SEEN_BT_MAX  256

typedef struct {
    uint32_t magic;
    uint32_t version;

    uint8_t team_count; /* echoids in the team (1..PARTY_MAX) */
    uint8_t lead; /* index into team[] */
    uint8_t unused_source; /* was the scan source (demo mode, now removed) */
    uint8_t uart; /* ScanUart */
    uint8_t bt_enabled;
    uint8_t reserved[3];

    /* Board configuration (zero = default) */
    uint8_t baud_idx;
    uint8_t power;
    uint8_t boot_idx;
    uint8_t eol;
    uint8_t wifi_cmd;
    uint8_t bt_cmd;
    uint8_t log_sd;
    uint8_t reserved2[9];

    uint32_t pacs; /* crafted PACs, each holds PAC_SIZE echoids; no limit */
    uint32_t pac_count; /* echoids stored in pac.bin */

    Inventory inv;

    uint32_t captures;
    uint32_t wins;
    uint32_t shards_found;

    /* Bluetooth shard cooldowns: device hash and when it last gave a shard (RTC s). */
    uint32_t seen_bt[SEEN_BT_MAX];
    uint32_t seen_bt_time[SEEN_BT_MAX];
    uint16_t seen_bt_count;
    uint16_t dungeons_cleared; /* was reserved (zero in older saves) */

    Echoid team[PARTY_MAX];
} SaveData;

/* How many echoids the PAC storage can hold. */
static inline uint32_t save_pac_capacity(const SaveData* sv) {
    uint64_t cap = (uint64_t)PAC_SIZE * sv->pacs;
    return cap > UINT32_MAX ? UINT32_MAX : (uint32_t)cap;
}

/* Returns false if no (valid) save exists. Older save versions are upgraded
 * (and their stored echoids moved into the PAC file). */
bool save_load(SaveData* data);
bool save_store(const SaveData* data);
void save_delete(void);
