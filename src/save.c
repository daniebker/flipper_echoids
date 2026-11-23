#include "save.h"

#include <furi.h>
#include <storage/storage.h>

#define SAVE_PATH APP_DATA_PATH("save.bin")

bool save_load(SaveData* data) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(file, SAVE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        ok = storage_file_read(file, data, sizeof(SaveData)) == sizeof(SaveData);
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    if(ok && (data->magic != SAVE_MAGIC || data->version != SAVE_VERSION)) ok = false;
    if(ok && (data->team_count == 0 || data->team_count > MAX_TEAM)) ok = false;
    if(ok && data->lead >= data->team_count) data->lead = 0;
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
