/* Furi core, HAL, records, notifications and storage for the shim. */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "furi.h"
#include "furi_hal.h"
#include "notification/notification_messages.h"
#include "shim.h"
#include "storage/storage.h"

/* ------------------------------------------------------------------------- */
/* Time                                                                      */
/* ------------------------------------------------------------------------- */

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

uint32_t furi_get_tick(void) {
    return (uint32_t)now_ms();
}

uint32_t furi_ms_to_ticks(uint32_t ms) {
    return ms;
}

void furi_delay_ms(uint32_t ms) {
    usleep((useconds_t)ms * 1000u);
}

/* ------------------------------------------------------------------------- */
/* Mutex                                                                     */
/* ------------------------------------------------------------------------- */

struct FuriMutex {
    pthread_mutex_t m;
};

FuriMutex* furi_mutex_alloc(FuriMutexType type) {
    FuriMutex* mx = malloc(sizeof(FuriMutex));
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    if(type == FuriMutexTypeRecursive) pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mx->m, &attr);
    pthread_mutexattr_destroy(&attr);
    return mx;
}

void furi_mutex_free(FuriMutex* mx) {
    pthread_mutex_destroy(&mx->m);
    free(mx);
}

FuriStatus furi_mutex_acquire(FuriMutex* mx, uint32_t timeout) {
    if(timeout == FuriWaitForever) {
        return pthread_mutex_lock(&mx->m) == 0 ? FuriStatusOk : FuriStatusError;
    }
    uint64_t end = now_ms() + timeout;
    for(;;) {
        if(pthread_mutex_trylock(&mx->m) == 0) return FuriStatusOk;
        if(now_ms() >= end) return FuriStatusErrorTimeout;
        usleep(500);
    }
}

FuriStatus furi_mutex_release(FuriMutex* mx) {
    return pthread_mutex_unlock(&mx->m) == 0 ? FuriStatusOk : FuriStatusError;
}

/* ------------------------------------------------------------------------- */
/* Message queue                                                             */
/* ------------------------------------------------------------------------- */

struct FuriMessageQueue {
    pthread_mutex_t m;
    pthread_cond_t cond;
    uint8_t* buf;
    uint32_t size, count, head, used;
};

FuriMessageQueue* furi_message_queue_alloc(uint32_t msg_count, uint32_t msg_size) {
    FuriMessageQueue* q = calloc(1, sizeof(FuriMessageQueue));
    pthread_mutex_init(&q->m, NULL);
    pthread_cond_init(&q->cond, NULL);
    q->buf = malloc((size_t)msg_count * msg_size);
    q->size = msg_size;
    q->count = msg_count;
    return q;
}

void furi_message_queue_free(FuriMessageQueue* q) {
    pthread_cond_destroy(&q->cond);
    pthread_mutex_destroy(&q->m);
    free(q->buf);
    free(q);
}

static void deadline(struct timespec* ts, uint32_t timeout) {
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += timeout / 1000u;
    ts->tv_nsec += (long)(timeout % 1000u) * 1000000L;
    if(ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

FuriStatus furi_message_queue_put(FuriMessageQueue* q, const void* msg, uint32_t timeout) {
    UNUSED(timeout);
    pthread_mutex_lock(&q->m);
    if(q->used == q->count) {
        /* Full: drop the message rather than block a UI thread. */
        pthread_mutex_unlock(&q->m);
        return FuriStatusErrorTimeout;
    }
    uint32_t slot = (q->head + q->used) % q->count;
    memcpy(q->buf + (size_t)slot * q->size, msg, q->size);
    q->used++;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->m);
    return FuriStatusOk;
}

FuriStatus furi_message_queue_get(FuriMessageQueue* q, void* msg, uint32_t timeout) {
    pthread_mutex_lock(&q->m);
    if(q->used == 0 && timeout > 0) {
        if(timeout == FuriWaitForever) {
            while(q->used == 0)
                pthread_cond_wait(&q->cond, &q->m);
        } else {
            struct timespec ts;
            deadline(&ts, timeout);
            while(q->used == 0) {
                if(pthread_cond_timedwait(&q->cond, &q->m, &ts) == ETIMEDOUT) break;
            }
        }
    }
    if(q->used == 0) {
        pthread_mutex_unlock(&q->m);
        return FuriStatusErrorTimeout;
    }
    memcpy(msg, q->buf + (size_t)q->head * q->size, q->size);
    q->head = (q->head + 1) % q->count;
    q->used--;
    pthread_mutex_unlock(&q->m);
    return FuriStatusOk;
}

/* ------------------------------------------------------------------------- */
/* Records                                                                   */
/* ------------------------------------------------------------------------- */

static int record_dummy;

void* furi_record_open(const char* name) {
    UNUSED(name);
    return &record_dummy;
}

void furi_record_close(const char* name) {
    UNUSED(name);
}

/* ------------------------------------------------------------------------- */
/* HAL                                                                       */
/* ------------------------------------------------------------------------- */

static uint8_t device_mac[6] = {0x02, 0x45, 0x43, 0x48, 0x4F, 0x31};
static char device_name[32] = "Phone";

void shim_set_device(const uint8_t mac[6], const char* name) {
    memcpy(device_mac, mac, 6);
    if(name) {
        strncpy(device_name, name, sizeof(device_name) - 1);
        device_name[sizeof(device_name) - 1] = '\0';
    }
}

const uint8_t* furi_hal_version_get_ble_mac(void) {
    return device_mac;
}

const char* furi_hal_version_get_name_ptr(void) {
    return device_name;
}

uint32_t furi_hal_random_get(void) {
    static uint64_t state = 0;
    if(!state) state = now_ms() ^ ((uint64_t)getpid() << 32) ^ (uint64_t)(uintptr_t)&state;
    /* splitmix64 */
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return (uint32_t)(z ^ (z >> 31));
}

bool furi_hal_power_is_otg_enabled(void) {
    return false;
}

bool furi_hal_power_enable_otg(void) {
    return false;
}

void furi_hal_power_disable_otg(void) {
}

uint32_t furi_hal_rtc_get_timestamp(void) {
    return (uint32_t)time(NULL);
}

void furi_hal_rtc_get_datetime(DateTime* dt) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    dt->hour = (uint8_t)tm.tm_hour;
    dt->minute = (uint8_t)tm.tm_min;
    dt->second = (uint8_t)tm.tm_sec;
    dt->day = (uint8_t)tm.tm_mday;
    dt->month = (uint8_t)(tm.tm_mon + 1);
    dt->year = (uint16_t)(tm.tm_year + 1900);
    dt->weekday = (uint8_t)(tm.tm_wday == 0 ? 7 : tm.tm_wday);
}

/* ------------------------------------------------------------------------- */
/* Notifications                                                             */
/* ------------------------------------------------------------------------- */

const NotificationSequence sequence_blink_green_10 = {0};
const NotificationSequence sequence_blink_cyan_10 = {0};
const NotificationSequence sequence_blink_magenta_100 = {0};
const NotificationSequence sequence_single_vibro = {60};
const NotificationSequence sequence_success = {40};
const NotificationSequence sequence_semi_success = {25};
const NotificationSequence sequence_error = {120};

static volatile int vibration_ms;

void notification_message(NotificationApp* app, const NotificationSequence* sequence) {
    UNUSED(app);
    if(sequence && sequence->vibrate_ms > vibration_ms) vibration_ms = sequence->vibrate_ms;
}

int shim_take_vibration(void) {
    int ms = vibration_ms;
    vibration_ms = 0;
    return ms;
}

/* ------------------------------------------------------------------------- */
/* Storage                                                                   */
/* ------------------------------------------------------------------------- */

static char data_dir[512] = ".";

void shim_set_data_dir(const char* dir) {
    strncpy(data_dir, dir, sizeof(data_dir) - 1);
    data_dir[sizeof(data_dir) - 1] = '\0';
}

/* Maps Flipper paths onto the app's files directory. */
static void map_path(const char* path, char* out, size_t size) {
    static const char* const prefixes[] = {"/data/", "/ext/apps_data/echoids/"};
    for(size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        size_t n = strlen(prefixes[i]);
        if(strncmp(path, prefixes[i], n) == 0) {
            snprintf(out, size, "%s/%s", data_dir, path + n);
            return;
        }
    }
    if(strcmp(path, "/ext/apps_data/echoids") == 0 || strcmp(path, "/data") == 0) {
        snprintf(out, size, "%s", data_dir);
        return;
    }
    snprintf(out, size, "%s", path);
}

struct File {
    FILE* f;
};

File* storage_file_alloc(Storage* storage) {
    UNUSED(storage);
    return calloc(1, sizeof(File));
}

void storage_file_free(File* file) {
    if(file->f) fclose(file->f);
    free(file);
}

bool storage_file_open(File* file, const char* path, FS_AccessMode access, FS_OpenMode mode) {
    char real[600];
    map_path(path, real, sizeof(real));
    bool write = (access & FSAM_WRITE) != 0;
    bool read = (access & FSAM_READ) != 0;
    struct stat st;
    bool exists = stat(real, &st) == 0;
    const char* m = NULL;
    switch(mode) {
    case FSOM_OPEN_EXISTING:
        if(!exists) return false;
        m = write ? "r+b" : "rb";
        break;
    case FSOM_OPEN_ALWAYS:
        m = exists ? (write ? "r+b" : "rb") : "w+b";
        break;
    case FSOM_OPEN_APPEND:
        m = read ? "a+b" : "ab";
        break;
    case FSOM_CREATE_NEW:
        if(exists) return false;
        m = "w+b";
        break;
    case FSOM_CREATE_ALWAYS:
        m = read ? "w+b" : "wb";
        break;
    }
    if(!m) return false;
    file->f = fopen(real, m);
    return file->f != NULL;
}

bool storage_file_close(File* file) {
    if(!file->f) return false;
    fclose(file->f);
    file->f = NULL;
    return true;
}

size_t storage_file_read(File* file, void* buf, size_t size) {
    return file->f ? fread(buf, 1, size, file->f) : 0;
}

size_t storage_file_write(File* file, const void* buf, size_t size) {
    return file->f ? fwrite(buf, 1, size, file->f) : 0;
}

bool storage_file_seek(File* file, uint32_t offset, bool from_start) {
    if(!file->f) return false;
    if(from_start && (uint64_t)offset > storage_file_size(file)) return false;
    return fseek(file->f, (long)offset, from_start ? SEEK_SET : SEEK_CUR) == 0;
}

uint64_t storage_file_tell(File* file) {
    return file->f ? (uint64_t)ftell(file->f) : 0;
}

uint64_t storage_file_size(File* file) {
    if(!file->f) return 0;
    fflush(file->f);
    struct stat st;
    if(fstat(fileno(file->f), &st) != 0) return 0;
    return (uint64_t)st.st_size;
}

bool storage_file_truncate(File* file) {
    if(!file->f) return false;
    fflush(file->f);
    return ftruncate(fileno(file->f), ftell(file->f)) == 0;
}

bool storage_file_sync(File* file) {
    return file->f && fflush(file->f) == 0;
}

bool storage_simply_remove(Storage* storage, const char* path) {
    UNUSED(storage);
    char real[600];
    map_path(path, real, sizeof(real));
    return remove(real) == 0 || errno == ENOENT;
}

bool storage_simply_mkdir(Storage* storage, const char* path) {
    UNUSED(storage);
    char real[600];
    map_path(path, real, sizeof(real));
    return mkdir(real, 0755) == 0 || errno == EEXIST;
}

FS_Error storage_common_copy(Storage* storage, const char* old_path, const char* new_path) {
    UNUSED(storage);
    char a[600], b[600];
    map_path(old_path, a, sizeof(a));
    map_path(new_path, b, sizeof(b));
    FILE* in = fopen(a, "rb");
    if(!in) return FSE_NOT_EXIST;
    FILE* out = fopen(b, "wb");
    if(!out) {
        fclose(in);
        return FSE_INTERNAL;
    }
    char buf[4096];
    size_t n;
    while((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return FSE_OK;
}
volatile int echoids_save_request;
