#pragma once

#include "game.h"
#include "scanner.h"

#define SAVE_MAGIC   0x45434831u /* "ECH1" */
#define SAVE_VERSION 1
#define SEEN_BT_MAX  256

typedef struct {
    uint32_t magic;
    uint32_t version;

    uint8_t team_count;
    uint8_t lead;
    uint8_t source; /* ScanSource */
    uint8_t uart; /* ScanUart */
    uint8_t bt_enabled;
    uint8_t reserved[3];

    Echoid team[MAX_TEAM];
    Inventory inv;

    /* Hashes of Bluetooth MACs that already gave a shard. */
    uint32_t seen_bt[SEEN_BT_MAX];
    uint16_t seen_bt_count;
    uint16_t seen_bt_pos;

    uint32_t captures;
    uint32_t wins;
    uint32_t shards_found;
} SaveData;

/* Returns false if no (valid) save exists. */
bool save_load(SaveData* data);
bool save_store(const SaveData* data);
void save_delete(void);
