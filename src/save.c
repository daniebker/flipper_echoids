#include "save.h"
#include "pac_store.h"

#include <furi.h>
#include <furi_hal_rtc.h>
#include <storage/storage.h>
#include <string.h>

#define SAVE_PATH        APP_DATA_PATH("save.bin")
#define SAVE_BACKUP_PATH APP_DATA_PATH("save_old_version.bin")

/* ---- Save versions 1-6 stored 5 item counts (no spare item slots) ---- */
typedef struct {
    uint16_t shards[ECHO_TYPE_COUNT][SHARD_TIER_COUNT];
    uint16_t items[5];
} InventoryV6;

/* ---- Save versions 1-4: a fixed table of 40 echoids, no PACs to craft ---- */
/* ---- Save version 5: one table of up to 106 echoids, flags mark the team ---- */

#define LEGACY_MAX 40
#define LEGACY_PACS 4 /* 6 + 4 * 10 >= 40: old players keep the room they had */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint8_t team_count;
    uint8_t lead;
    uint8_t source;
    uint8_t uart;
    uint8_t bt_enabled;
    uint8_t reserved[3];
    Echoid team[LEGACY_MAX];
    InventoryV6 inv;
    uint32_t seen_bt[SEEN_BT_MAX];
    uint16_t seen_bt_count;
    uint16_t seen_bt_pos;
    uint32_t captures;
    uint32_t wins;
    uint32_t shards_found;
    /* v2 */
    uint8_t baud_idx;
    uint8_t power;
    uint8_t boot_idx;
    uint8_t eol;
    uint8_t wifi_cmd;
    uint8_t bt_cmd;
    uint8_t log_sd;
    uint8_t reserved2[9];
    /* v3 */
    uint32_t seen_bt_time[SEEN_BT_MAX];
} SaveDataV4;

#define V1_SIZE (offsetof(SaveDataV4, baud_idx))
#define V2_SIZE (offsetof(SaveDataV4, seen_bt_time))

/* Brings a v1-v4 save up to the v4 layout. Returns false if it isn't valid. */
static bool legacy_upgrade(SaveDataV4* old, size_t read) {
    if(old->version == 4 || old->version == 3) {
        if(read != sizeof(SaveDataV4)) return false;
    } else if(old->version == 1 || old->version == 2) {
        size_t keep = old->version == 1 ? V1_SIZE : V2_SIZE;
        if(read < keep) return false;
        /* Fields added since start at their zero defaults. */
        memset((uint8_t*)old + keep, 0, sizeof(SaveDataV4) - keep);
        /* Devices that already gave a shard start their cooldown now. */
        uint32_t now = furi_hal_rtc_get_timestamp();
        for(uint16_t i = 0; i < SEEN_BT_MAX; i++)
            old->seen_bt_time[i] = now;
    } else {
        return false;
    }
    if(old->team_count == 0 || old->team_count > LEGACY_MAX) return false;
    if(old->lead >= old->team_count) old->lead = 0;
    if(old->version < 4) {
        /* Before v4 everything was "the team": keep the lead plus the
         * first five others in the team and move the rest to the PAC. */
        for(uint8_t i = 0; i < old->team_count; i++)
            old->team[i].flags = 0;
        roster_normalize(old->team, old->team_count, &old->lead, true);
    }
    return true;
}

#define V5_MAX 106

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint8_t team_count;
    uint8_t lead;
    uint8_t source;
    uint8_t uart;
    uint8_t bt_enabled;
    uint8_t pacs;
    uint8_t reserved[2];
    uint8_t baud_idx;
    uint8_t power;
    uint8_t boot_idx;
    uint8_t eol;
    uint8_t wifi_cmd;
    uint8_t bt_cmd;
    uint8_t log_sd;
    uint8_t reserved2[9];
    InventoryV6 inv;
    uint32_t captures;
    uint32_t wins;
    uint32_t shards_found;
    uint32_t seen_bt[SEEN_BT_MAX];
    uint32_t seen_bt_time[SEEN_BT_MAX];
    uint16_t seen_bt_count;
    uint16_t reserved3;
    Echoid team[V5_MAX];
} SaveDataV5;

static void v4_to_v5(const SaveDataV4* old, SaveDataV5* data) {
    memset(data, 0, sizeof(*data));
    data->magic = SAVE_MAGIC;
    data->version = 5;
    data->team_count = old->team_count;
    data->lead = old->lead;
    data->uart = old->uart;
    data->bt_enabled = old->bt_enabled;
    data->pacs = LEGACY_PACS;
    data->baud_idx = old->baud_idx;
    data->power = old->power;
    data->boot_idx = old->boot_idx;
    data->eol = old->eol;
    data->wifi_cmd = old->wifi_cmd;
    data->bt_cmd = old->bt_cmd;
    data->log_sd = old->log_sd;
    data->inv = old->inv;
    data->captures = old->captures;
    data->wins = old->wins;
    data->shards_found = old->shards_found;
    memcpy(data->seen_bt, old->seen_bt, sizeof(data->seen_bt));
    memcpy(data->seen_bt_time, old->seen_bt_time, sizeof(data->seen_bt_time));
    data->seen_bt_count = old->seen_bt_count;
    memcpy(data->team, old->team, sizeof(Echoid) * old->team_count);
}

/* ---- Save version 6: the current layout, but with InventoryV6 ---- */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint8_t team_count;
    uint8_t lead;
    uint8_t source;
    uint8_t uart;
    uint8_t bt_enabled;
    uint8_t reserved[3];
    uint8_t baud_idx;
    uint8_t power;
    uint8_t boot_idx;
    uint8_t eol;
    uint8_t wifi_cmd;
    uint8_t bt_cmd;
    uint8_t log_sd;
    uint8_t reserved2[9];
    uint32_t pacs;
    uint32_t pac_count;
    InventoryV6 inv;
    uint32_t captures;
    uint32_t wins;
    uint32_t shards_found;
    uint32_t seen_bt[SEEN_BT_MAX];
    uint32_t seen_bt_time[SEEN_BT_MAX];
    uint16_t seen_bt_count;
    uint16_t reserved3;
    Echoid team[PARTY_MAX];
} SaveDataV6;

/* v5 -> v6: team members stay in the save, the rest are written to pac.bin. */
static bool v5_to_v6(SaveDataV5* old, SaveDataV6* data) {
    if(old->team_count == 0 || old->team_count > V5_MAX) return false;
    if(old->lead >= old->team_count) old->lead = 0;
    roster_normalize(old->team, old->team_count, &old->lead, false);

    memset(data, 0, sizeof(*data));
    data->magic = SAVE_MAGIC;
    data->version = 6;
    data->uart = old->uart;
    data->bt_enabled = old->bt_enabled;
    data->baud_idx = old->baud_idx;
    data->power = old->power;
    data->boot_idx = old->boot_idx;
    data->eol = old->eol;
    data->wifi_cmd = old->wifi_cmd;
    data->bt_cmd = old->bt_cmd;
    data->log_sd = old->log_sd;
    data->pacs = old->pacs ? old->pacs : 1;
    data->inv = old->inv;
    data->captures = old->captures;
    data->wins = old->wins;
    data->shards_found = old->shards_found;
    memcpy(data->seen_bt, old->seen_bt, sizeof(data->seen_bt));
    memcpy(data->seen_bt_time, old->seen_bt_time, sizeof(data->seen_bt_time));
    data->seen_bt_count = old->seen_bt_count;

    pac_clear();
    for(uint8_t i = 0; i < old->team_count; i++) {
        Echoid e = old->team[i];
        bool in_team = (e.flags & ECHO_FLAG_PARTY) != 0;
        e.flags = 0;
        if(in_team && data->team_count < PARTY_MAX) {
            if(i == old->lead) data->lead = data->team_count;
            data->team[data->team_count++] = e;
        } else {
            if(!pac_append(&e)) return false;
            data->pac_count++;
        }
    }
    /* Never shrink the room the player already had. */
    while((uint64_t)PAC_SIZE * data->pacs < data->pac_count)
        data->pacs++;
    return data->team_count > 0;
}

/* v6 -> v7: the inventory gains spare item slots; everything else is copied. */
static void v6_to_v7(const SaveDataV6* old, SaveData* data) {
    memset(data, 0, sizeof(*data));
    data->magic = SAVE_MAGIC;
    data->version = SAVE_VERSION;
    data->team_count = old->team_count;
    data->lead = old->lead;
    data->uart = old->uart;
    data->bt_enabled = old->bt_enabled;
    data->baud_idx = old->baud_idx;
    data->power = old->power;
    data->boot_idx = old->boot_idx;
    data->eol = old->eol;
    data->wifi_cmd = old->wifi_cmd;
    data->bt_cmd = old->bt_cmd;
    data->log_sd = old->log_sd;
    data->pacs = old->pacs;
    data->pac_count = old->pac_count;
    memcpy(data->inv.shards, old->inv.shards, sizeof(old->inv.shards));
    for(int i = 0; i < 5; i++)
        data->inv.items[i] = old->inv.items[i];
    data->captures = old->captures;
    data->wins = old->wins;
    data->shards_found = old->shards_found;
    memcpy(data->seen_bt, old->seen_bt, sizeof(data->seen_bt));
    memcpy(data->seen_bt_time, old->seen_bt_time, sizeof(data->seen_bt_time));
    data->seen_bt_count = old->seen_bt_count;
    memcpy(data->team, old->team, sizeof(data->team));
}

/* ---------------------------------------------------------------------------- */

static size_t read_save(void* buf, size_t size) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    size_t read = 0;
    if(storage_file_open(file, SAVE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        read = storage_file_read(file, buf, size);
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return read;
}

bool save_load(SaveData* data) {
    bool ok = false;
    bool upgraded = false;
    memset(data, 0, sizeof(SaveData));
    size_t read = read_save(data, sizeof(SaveData));
    if(read < 8 || data->magic != SAVE_MAGIC) return false;

    if(data->version == SAVE_VERSION) {
        ok = read == sizeof(SaveData);
    } else if(data->version == 7) {
        /* v7 -> v8: only the (empty) defeated list is new. */
        ok = read == SAVE_V7_SIZE;
        if(ok) {
            memset((uint8_t*)data + SAVE_V7_SIZE, 0, sizeof(SaveData) - SAVE_V7_SIZE);
            data->version = SAVE_VERSION;
            upgraded = true;
        }
    } else if(data->version >= 1 && data->version <= 6) {
        /* Keep a copy of the old file first: if anything goes wrong below,
         * the player's progress is still on the card. */
        uint32_t version = data->version;
        Storage* storage = furi_record_open(RECORD_STORAGE);
        storage_simply_remove(storage, SAVE_BACKUP_PATH);
        storage_common_copy(storage, SAVE_PATH, SAVE_BACKUP_PATH);
        furi_record_close(RECORD_STORAGE);
        /* Old layout: read it again into the legacy structs and convert. */
        SaveDataV6* v6 = malloc(sizeof(SaveDataV6));
        memset(v6, 0, sizeof(SaveDataV6));
        if(version == 6) {
            ok = read_save(v6, sizeof(SaveDataV6)) == sizeof(SaveDataV6);
        } else {
            SaveDataV5* v5 = malloc(sizeof(SaveDataV5));
            memset(v5, 0, sizeof(SaveDataV5));
            if(version == 5) {
                ok = read_save(v5, sizeof(SaveDataV5)) == sizeof(SaveDataV5);
            } else {
                SaveDataV4* v4 = malloc(sizeof(SaveDataV4));
                memset(v4, 0, sizeof(SaveDataV4));
                size_t old_read = read_save(v4, sizeof(SaveDataV4));
                ok = legacy_upgrade(v4, old_read);
                if(ok) v4_to_v5(v4, v5);
                free(v4);
            }
            /* v5 -> v6 also moves stored echoids into the PAC file. */
            if(ok) ok = v5_to_v6(v5, v6);
            free(v5);
        }
        if(ok) v6_to_v7(v6, data);
        free(v6);
        upgraded = ok;
    }
    if(!ok) return false;

    if(data->team_count == 0 || data->team_count > PARTY_MAX) return false;
    if(data->lead >= data->team_count) data->lead = 0;
    for(uint8_t i = 0; i < data->team_count; i++)
        data->team[i].flags = 0;
    if(data->pacs < 1) data->pacs = 1;
    /* Trust the PAC file over the saved count if they disagree. */
    uint32_t stored = pac_file_count();
    if(data->pac_count > stored) data->pac_count = stored;
    if(data->seen_bt_count > SEEN_BT_MAX) data->seen_bt_count = SEEN_BT_MAX;
    if(data->defeated_count > DEFEATED_MAX) data->defeated_count = DEFEATED_MAX;
    if(data->defeated_next >= DEFEATED_MAX) data->defeated_next = 0;
    /* Store the converted save right away, so the PAC file isn't rebuilt again. */
    if(upgraded) save_store(data);
    return true;
}

bool save_store(const SaveData* data) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(file, SAVE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        ok = storage_file_write(file, data, sizeof(SaveData)) == sizeof(SaveData);
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

void save_delete(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_remove(storage, SAVE_PATH);
    furi_record_close(RECORD_STORAGE);
    pac_clear();
}
