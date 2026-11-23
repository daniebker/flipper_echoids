#include "pac_store.h"

#include <furi.h>
#include <storage/storage.h>
#include <string.h>

#define PAC_PATH   APP_DATA_PATH("pac.bin")
#define REC        ((uint32_t)sizeof(Echoid))
#define FIND_CHUNK 16

typedef struct {
    Storage* storage;
    File* file;
} PacFile;

static bool pac_open(PacFile* pf, FS_AccessMode access, FS_OpenMode mode) {
    pf->storage = furi_record_open(RECORD_STORAGE);
    pf->file = storage_file_alloc(pf->storage);
    if(storage_file_open(pf->file, PAC_PATH, access, mode)) return true;
    storage_file_free(pf->file);
    furi_record_close(RECORD_STORAGE);
    pf->file = NULL;
    return false;
}

static void pac_close(PacFile* pf) {
    storage_file_close(pf->file);
    storage_file_free(pf->file);
    furi_record_close(RECORD_STORAGE);
}

uint32_t pac_file_count(void) {
    PacFile pf;
    if(!pac_open(&pf, FSAM_READ, FSOM_OPEN_EXISTING)) return 0;
    uint32_t n = (uint32_t)(storage_file_size(pf.file) / REC);
    pac_close(&pf);
    return n;
}

bool pac_append(const Echoid* e) {
    PacFile pf;
    if(!pac_open(&pf, FSAM_WRITE, FSOM_OPEN_APPEND)) return false;
    bool ok = storage_file_write(pf.file, e, REC) == REC;
    pac_close(&pf);
    return ok;
}

bool pac_read(uint32_t idx, Echoid* out) {
    return pac_read_page(idx, out, 1) == 1;
}

bool pac_write(uint32_t idx, const Echoid* e) {
    PacFile pf;
    if(!pac_open(&pf, FSAM_READ_WRITE, FSOM_OPEN_EXISTING)) return false;
    bool ok = storage_file_seek(pf.file, idx * REC, true) &&
              storage_file_write(pf.file, e, REC) == REC;
    pac_close(&pf);
    return ok;
}

uint8_t pac_read_page(uint32_t start, Echoid* out, uint8_t n) {
    PacFile pf;
    if(!pac_open(&pf, FSAM_READ, FSOM_OPEN_EXISTING)) return 0;
    uint8_t got = 0;
    if(storage_file_seek(pf.file, start * REC, true)) {
        size_t bytes = storage_file_read(pf.file, out, REC * n);
        got = (uint8_t)(bytes / REC);
    }
    pac_close(&pf);
    return got;
}

bool pac_remove(uint32_t idx, uint32_t count) {
    if(idx >= count) return false;
    PacFile pf;
    if(!pac_open(&pf, FSAM_READ_WRITE, FSOM_OPEN_EXISTING)) return false;
    bool ok = true;
    /* Shift every later record down by one, one at a time. */
    Echoid e;
    for(uint32_t i = idx + 1; i < count && ok; i++) {
        ok = storage_file_seek(pf.file, i * REC, true) &&
             storage_file_read(pf.file, &e, REC) == REC &&
             storage_file_seek(pf.file, (i - 1) * REC, true) &&
             storage_file_write(pf.file, &e, REC) == REC;
    }
    if(ok) ok = storage_file_seek(pf.file, (count - 1) * REC, true) && storage_file_truncate(pf.file);
    pac_close(&pf);
    return ok;
}

uint32_t pac_find(const uint8_t mac[6], uint32_t count) {
    PacFile pf;
    if(count == 0 || !pac_open(&pf, FSAM_READ, FSOM_OPEN_EXISTING)) return PAC_NOT_FOUND;
    Echoid chunk[FIND_CHUNK];
    uint32_t found = PAC_NOT_FOUND;
    for(uint32_t base = 0; base < count && found == PAC_NOT_FOUND; base += FIND_CHUNK) {
        size_t got = storage_file_read(pf.file, chunk, REC * FIND_CHUNK) / REC;
        if(got == 0) break;
        for(size_t i = 0; i < got && base + i < count; i++) {
            if(memcmp(chunk[i].mac, mac, 6) == 0) {
                found = base + (uint32_t)i;
                break;
            }
        }
    }
    pac_close(&pf);
    return found;
}

void pac_clear(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_remove(storage, PAC_PATH);
    furi_record_close(RECORD_STORAGE);
}
