#pragma once

#include "game.h"
#include "scanner.h"

#define SAVE_MAGIC   0x45434831u /* "ECH1" */
#define SAVE_VERSION 5 /* v5: crafted PACs, room for MAX_OWNED echoids */
#define SEEN_BT_MAX  256

typedef struct {
    uint32_t magic;
    uint32_t version;

    uint8_t team_count; /* echoids owned (team + PAC); team members have ECHO_FLAG_PARTY */
    uint8_t lead;
    uint8_t source; /* ScanSource */
    uint8_t uart; /* ScanUart */
    uint8_t bt_enabled;
    uint8_t pacs; /* crafted PACs, each holds PAC_SIZE echoids */
    uint8_t reserved[2];

    /* Board configuration (zero = default) */
    uint8_t baud_idx;
    uint8_t power;
    uint8_t boot_idx;
    uint8_t eol;
    uint8_t wifi_cmd;
    uint8_t bt_cmd;
    uint8_t log_sd;
    uint8_t reserved2[9];

    Inventory inv;

    uint32_t captures;
    uint32_t wins;
    uint32_t shards_found;

    /* Bluetooth shard cooldowns: device hash and when it last gave a shard (RTC s). */
    uint32_t seen_bt[SEEN_BT_MAX];
    uint32_t seen_bt_time[SEEN_BT_MAX];
    uint16_t seen_bt_count;
    uint16_t reserved3;

    Echoid team[MAX_OWNED];
} SaveData;

/* How many echoids the player can own: the team plus every PAC. */
static inline uint8_t save_capacity(const SaveData* sv) {
    uint32_t cap = PARTY_MAX + (uint32_t)PAC_SIZE * sv->pacs;
    return (uint8_t)(cap > MAX_OWNED ? MAX_OWNED : cap);
}

/* Returns false if no (valid) save exists. Older save versions are upgraded. */
bool save_load(SaveData* data);
bool save_store(const SaveData* data);
void save_delete(void);
