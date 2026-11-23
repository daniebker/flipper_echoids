/*
 * Cards whose echoid is finished with: caught or defeated. Those cards can't
 * start a battle again. Kept in their own file (a flat list of the echoids'
 * MACs), so the save format doesn't change.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

bool card_done(const uint8_t mac[6]);
bool card_mark_done(const uint8_t mac[6]);
void card_clear(void);
