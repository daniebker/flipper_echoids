#include "card_reader.h"

#include <furi.h>
#include <string.h>

#include <nfc/nfc.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a_poller.h>

#include <lfrfid/lfrfid_worker.h>
#include <lfrfid/protocols/lfrfid_protocols.h>
#include <toolbox/protocols/protocol_dict.h>

struct CardReader {
    FuriMutex* mutex;
    bool running;
    uint8_t kind;

    /* NFC */
    Nfc* nfc;
    NfcPoller* poller;

    /* 125 kHz RFID */
    ProtocolDict* dict;
    LFRFIDWorker* worker;
    bool rfid_done; /* a read finished: start the next one */

    bool have;
    uint8_t uid[CARD_UID_MAX];
    uint8_t uid_len;
};

static void publish(CardReader* r, const uint8_t* uid, size_t len) {
    if(len == 0) return;
    if(len > CARD_UID_MAX) len = CARD_UID_MAX;
    furi_mutex_acquire(r->mutex, FuriWaitForever);
    memcpy(r->uid, uid, len);
    r->uid_len = (uint8_t)len;
    r->have = true;
    furi_mutex_release(r->mutex);
}

/* Runs on the NFC worker thread. Keeps polling while the card stays. */
static NfcCommand nfc_callback(NfcGenericEvent event, void* ctx) {
    CardReader* r = ctx;
    if(event.protocol == NfcProtocolIso14443_3a) {
        const Iso14443_3aPollerEvent* e = event.event_data;
        if(e->type == Iso14443_3aPollerEventTypeReady) {
            const Iso14443_3aData* d = nfc_poller_get_data(r->poller);
            if(d) publish(r, d->uid, d->uid_len);
            furi_delay_ms(150);
        }
    }
    return NfcCommandContinue;
}

/* Runs on the RFID worker thread. */
static void rfid_callback(LFRFIDWorkerReadResult result, ProtocolId protocol, void* ctx) {
    CardReader* r = ctx;
    if(result != LFRFIDWorkerReadDone) return;
    /* The protocol is part of the card's identity: the same bytes in two
     * protocols are two different cards. */
    uint8_t buf[CARD_UID_MAX];
    size_t size = protocol_dict_get_data_size(r->dict, protocol);
    if(size > CARD_UID_MAX - 1) size = CARD_UID_MAX - 1;
    buf[0] = (uint8_t)protocol;
    protocol_dict_get_data(r->dict, protocol, buf + 1, size);
    publish(r, buf, size + 1);
    furi_mutex_acquire(r->mutex, FuriWaitForever);
    r->rfid_done = true;
    furi_mutex_release(r->mutex);
}

CardReader* card_reader_alloc(void) {
    CardReader* r = malloc(sizeof(CardReader));
    memset(r, 0, sizeof(CardReader));
    r->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    return r;
}

void card_reader_free(CardReader* r) {
    card_reader_stop(r);
    furi_mutex_free(r->mutex);
    free(r);
}

void card_reader_stop(CardReader* r) {
    if(!r->running) return;
    if(r->poller) {
        nfc_poller_stop(r->poller);
        nfc_poller_free(r->poller);
        r->poller = NULL;
    }
    if(r->nfc) {
        nfc_free(r->nfc);
        r->nfc = NULL;
    }
    if(r->worker) {
        lfrfid_worker_stop(r->worker);
        lfrfid_worker_stop_thread(r->worker);
        lfrfid_worker_free(r->worker);
        r->worker = NULL;
    }
    if(r->dict) {
        protocol_dict_free(r->dict);
        r->dict = NULL;
    }
    r->running = false;
    r->have = false;
}

bool card_reader_start(CardReader* r, uint8_t kind) {
    card_reader_stop(r);
    r->kind = kind;
    r->have = false;
    r->rfid_done = false;
    if(kind == CardRfid) {
        r->dict = protocol_dict_alloc(lfrfid_protocols, LFRFIDProtocolMax);
        r->worker = lfrfid_worker_alloc(r->dict);
        lfrfid_worker_start_thread(r->worker);
        lfrfid_worker_read_start(r->worker, LFRFIDWorkerReadTypeAuto, rfid_callback, r);
    } else {
        r->nfc = nfc_alloc();
        r->poller = nfc_poller_alloc(r->nfc, NfcProtocolIso14443_3a);
        nfc_poller_start(r->poller, nfc_callback, r);
    }
    r->running = true;
    return true;
}

bool card_reader_take(CardReader* r, uint8_t uid[CARD_UID_MAX], uint8_t* uid_len) {
    if(!r->running) return false;
    furi_mutex_acquire(r->mutex, FuriWaitForever);
    bool got = r->have;
    if(got) {
        memcpy(uid, r->uid, r->uid_len);
        *uid_len = r->uid_len;
        r->have = false;
    }
    bool restart = r->rfid_done;
    r->rfid_done = false;
    furi_mutex_release(r->mutex);
    /* An RFID read ends once it finds a card: start the next one. */
    if(restart && r->worker) {
        lfrfid_worker_stop(r->worker);
        lfrfid_worker_read_start(r->worker, LFRFIDWorkerReadTypeAuto, rfid_callback, r);
    }
    return got;
}
