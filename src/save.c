#include "save.h"

#include <furi.h>
#include <furi_hal_rtc.h>
#include <storage/storage.h>
#include <string.h>

#define SAVE_PATH APP_DATA_PATH("save.bin")

bool save_load(SaveData* data) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = false;
    size_t read = 0;
    memset(data, 0, sizeof(SaveData));
    if(storage_file_open(file, SAVE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        read = storage_file_read(file, data, sizeof(SaveData));
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    if(data->magic == SAVE_MAGIC) {
        if(data->version == SAVE_VERSION && read == sizeof(SaveData)) {
            ok = true;
        } else if(
            (data->version == 1 && read >= SAVE_V1_SIZE) ||
            (data->version == 2 && read >= SAVE_V2_SIZE) ||
            (data->version == 3 && read == sizeof(SaveData))) {
            if(data->version < 3) {
                /* Fields added since start at their zero defaults. */
                size_t keep = data->version == 1 ? SAVE_V1_SIZE : SAVE_V2_SIZE;
                memset((uint8_t*)data + keep, 0, sizeof(SaveData) - keep);
                /* Devices that already gave a shard start their cooldown now. */
                uint32_t now = furi_hal_rtc_get_timestamp();
                for(uint16_t i = 0; i < SEEN_BT_MAX; i++)
                    data->seen_bt_time[i] = now;
            }
            /* Before v4 everything was "the team": keep the lead plus the
             * first five others in the team and move the rest to the PAC. */
            uint8_t count = data->team_count <= MAX_TEAM ? data->team_count : MAX_TEAM;
            for(uint8_t i = 0; i < count; i++)
                data->team[i].flags = 0;
            if(data->lead >= count) data->lead = 0;
            roster_normalize(data->team, count, &data->lead, true);
            data->version = SAVE_VERSION;
            ok = true;
        }
    }
    if(ok && (data->team_count == 0 || data->team_count > MAX_TEAM)) ok = false;
    if(ok && data->lead >= data->team_count) data->lead = 0;
    if(ok) roster_normalize(data->team, data->team_count, &data->lead, false);
    if(ok && data->seen_bt_count > SEEN_BT_MAX) data->seen_bt_count = SEEN_BT_MAX;
    if(ok) data->seen_bt_pos %= SEEN_BT_MAX;
    return ok;
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
