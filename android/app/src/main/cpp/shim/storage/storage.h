#pragma once

/* File storage over stdio. "/data/..." (APP_DATA_PATH) maps to the app's own
 * files directory, set by shim_set_data_dir(). */
#include "../furi.h"

#define APP_DATA_PATH(path) "/data/" path

typedef struct Storage Storage;
typedef struct File File;

typedef enum {
    FSAM_READ = (1 << 0),
    FSAM_WRITE = (1 << 1),
    FSAM_READ_WRITE = FSAM_READ | FSAM_WRITE,
} FS_AccessMode;

typedef enum {
    FSOM_OPEN_EXISTING = 1,
    FSOM_OPEN_ALWAYS = 2,
    FSOM_OPEN_APPEND = 4,
    FSOM_CREATE_NEW = 8,
    FSOM_CREATE_ALWAYS = 16,
} FS_OpenMode;

typedef enum {
    FSE_OK = 0,
    FSE_NOT_EXIST,
    FSE_INTERNAL,
} FS_Error;

File* storage_file_alloc(Storage* storage);
void storage_file_free(File* file);
bool storage_file_open(File* file, const char* path, FS_AccessMode access, FS_OpenMode mode);
bool storage_file_close(File* file);
size_t storage_file_read(File* file, void* buf, size_t size);
size_t storage_file_write(File* file, const void* buf, size_t size);
bool storage_file_seek(File* file, uint32_t offset, bool from_start);
uint64_t storage_file_tell(File* file);
uint64_t storage_file_size(File* file);
bool storage_file_truncate(File* file);
bool storage_file_sync(File* file);
bool storage_simply_remove(Storage* storage, const char* path);
bool storage_simply_mkdir(Storage* storage, const char* path);
FS_Error storage_common_copy(Storage* storage, const char* old_path, const char* new_path);
