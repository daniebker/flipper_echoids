/*
 * Minimal stand-in for the Flipper Zero "furi" core API, so the game's own
 * C code runs unchanged on Android (and on a PC for testing).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNUSED(x) (void)(x)

typedef enum {
    FuriStatusOk = 0,
    FuriStatusError = -1,
    FuriStatusErrorTimeout = -2,
} FuriStatus;

#define FuriWaitForever 0xFFFFFFFFu

typedef enum {
    FuriMutexTypeNormal,
    FuriMutexTypeRecursive,
} FuriMutexType;

typedef struct FuriMutex FuriMutex;
typedef struct FuriMessageQueue FuriMessageQueue;

FuriMutex* furi_mutex_alloc(FuriMutexType type);
void furi_mutex_free(FuriMutex* mutex);
FuriStatus furi_mutex_acquire(FuriMutex* mutex, uint32_t timeout);
FuriStatus furi_mutex_release(FuriMutex* mutex);

FuriMessageQueue* furi_message_queue_alloc(uint32_t msg_count, uint32_t msg_size);
void furi_message_queue_free(FuriMessageQueue* queue);
FuriStatus furi_message_queue_put(FuriMessageQueue* queue, const void* msg, uint32_t timeout);
FuriStatus furi_message_queue_get(FuriMessageQueue* queue, void* msg, uint32_t timeout);

/* Ticks are milliseconds here. */
uint32_t furi_get_tick(void);
uint32_t furi_ms_to_ticks(uint32_t ms);
void furi_delay_ms(uint32_t ms);

#define RECORD_GUI          "gui"
#define RECORD_STORAGE      "storage"
#define RECORD_NOTIFICATION "notification"
#define RECORD_EXPANSION    "expansion"

void* furi_record_open(const char* name);
void furi_record_close(const char* name);
