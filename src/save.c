#include "save.h"

#include <furi.h>
#include <furi_hal_rtc.h>
#include <storage/storage.h>
#include <string.h>

#define SAVE_PATH APP_DATA_PATH("save.bin")

/* ---- Save versions 1-4: a fixed table of 40 echoids, no PACs to craft ---- */

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
    Inventory inv;
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

static void legacy_convert(const SaveDataV4* old, SaveData* data) {
    memset(data, 0, sizeof(*data));
    data->magic = SAVE_MAGIC;
    data->version = SAVE_VERSION;
    data->team_count = old->team_count;
    data->lead = old->lead;
    data->source = old->source;
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
    memset(data, 0, sizeof(SaveData));
    size_t read = read_save(data, sizeof(SaveData));
    if(read < 8 || data->magic != SAVE_MAGIC) return false;

    if(data->version == SAVE_VERSION) {
        ok = read == sizeof(SaveData);
    } else {
        /* Old layout: read it again into the legacy struct and convert. */
        SaveDataV4* old = malloc(sizeof(SaveDataV4));
        memset(old, 0, sizeof(SaveDataV4));
        size_t old_read = read_save(old, sizeof(SaveDataV4));
        ok = legacy_upgrade(old, old_read);
        if(ok) legacy_convert(old, data);
        free(old);
    }

    if(ok && (data->team_count == 0 || data->team_count > MAX_OWNED)) ok = false;
    if(!ok) return false;
    if(data->pacs < 1) data->pacs = 1;
    if(data->pacs > PAC_MAX) data->pacs = PAC_MAX;
    if(data->lead >= data->team_count) data->lead = 0;
    roster_normalize(data->team, data->team_count, &data->lead, false);
    if(data->seen_bt_count > SEEN_BT_MAX) data->seen_bt_count = SEEN_BT_MAX;
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
}
