/*
 * Reads NFC (ISO 14443-A: MIFARE, NTAG, most access and transport cards) and
 * 125 kHz RFID cards with the Flipper's own readers, one kind at a time.
 * Only the card's UID (for RFID: its protocol and data) is used.
 *
 * Flipper only (card_reader.c); the PC smoke test has a fake.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "game.h"

typedef struct CardReader CardReader;

CardReader* card_reader_alloc(void);
void card_reader_free(CardReader* r);
/* Starts reading cards of this kind (CardKind), stopping any other first. */
bool card_reader_start(CardReader* r, uint8_t kind);
void card_reader_stop(CardReader* r);
/* The last card read since the previous call, if any. It keeps reporting a
 * card while it stays on the reader. */
bool card_reader_take(CardReader* r, uint8_t uid[CARD_UID_MAX], uint8_t* uid_len);
