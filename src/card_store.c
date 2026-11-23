#include "card_store.h"

#include <furi.h>
#include <storage/storage.h>
#include <string.h>

#define CARDS_PATH APP_DATA_PATH("cards.bin")
#define CHUNK      32

bool card_done(const uint8_t mac[6]) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool found = false;
    if(storage_file_open(file, CARDS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint8_t buf[CHUNK * 6];
        size_t got;
        while(!found && (got = storage_file_read(file, buf, sizeof(buf)) / 6) > 0) {
            for(size_t i = 0; i < got && !found; i++)
                found = memcmp(buf + i * 6, mac, 6) == 0;
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return found;
}

bool card_mark_done(const uint8_t mac[6]) {
    if(card_done(mac)) return true;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = storage_file_open(file, CARDS_PATH, FSAM_WRITE, FSOM_OPEN_APPEND) &&
              storage_file_write(file, mac, 6) == 6;
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

void card_clear(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_remove(storage, CARDS_PATH);
    furi_record_close(RECORD_STORAGE);
}
