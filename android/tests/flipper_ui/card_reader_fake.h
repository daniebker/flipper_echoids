#pragma once

#include "card_reader.h"

/* Puts a card on the fake reader (len 0 takes it away). */
void card_reader_fake_present(uint8_t kind, const uint8_t* uid, uint8_t len);
