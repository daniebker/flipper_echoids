/* The PC smoke test's stand-in for the Flipper's NFC and RFID readers. */
#include "card_reader_fake.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct CardReader {
    bool running;
    uint8_t kind;
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static bool present;
static uint8_t present_kind, present_uid[CARD_UID_MAX], present_len;

void card_reader_fake_present(uint8_t kind, const uint8_t* uid, uint8_t len) {
    pthread_mutex_lock(&lock);
    present = len > 0;
    present_kind = kind;
    present_len = len > CARD_UID_MAX ? CARD_UID_MAX : len;
    if(present_len) memcpy(present_uid, uid, present_len);
    pthread_mutex_unlock(&lock);
}

CardReader* card_reader_alloc(void) {
    return calloc(1, sizeof(CardReader));
}

void card_reader_free(CardReader* r) {
    free(r);
}

bool card_reader_start(CardReader* r, uint8_t kind) {
    r->running = true;
    r->kind = kind;
    return true;
}

void card_reader_stop(CardReader* r) {
    r->running = false;
}

bool card_reader_take(CardReader* r, uint8_t uid[CARD_UID_MAX], uint8_t* uid_len) {
    pthread_mutex_lock(&lock);
    bool got = r->running && present && present_kind == r->kind;
    if(got) {
        memcpy(uid, present_uid, present_len);
        *uid_len = present_len;
    }
    pthread_mutex_unlock(&lock);
    return got;
}
