/*
 * The Android app's game API: a JSON view of the shared session
 * (src/session.c), which holds every game rule, so the phone and the Flipper
 * play the same. This layer only converts state and actions to and from JSON.
 *
 * Not thread safe: the caller serialises calls (the JNI layer holds a lock).
 * State is returned as JSON strings that the caller must free().
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "game.h"
#include "scanner.h"

/* Loads the save, or starts a new game with a starter from `mac`.
 * Returns true if a new game was started. */
bool engine_init(const uint8_t mac[6], const char* device_name);

/* Call a few times a second: processes sightings, regenerates HP, follows the
 * hour, and autosaves. */
void engine_tick(void);
void engine_save(void);
/* Saves and forgets the session, so engine_init() loads it again (tests). */
void engine_unload(void);

/* ---- Scanning ---- */
void engine_scan_set(bool on);
/* 0 = not scanning, 1 = WiFi, 2 = WiFi + Bluetooth (phone radios only). */
int engine_radios_wanted(void);
void engine_push_sighting(const ScanEvent* ev);
void engine_set_radio_error(const char* error); /* NULL clears */

/* ---- State (JSON, caller frees) ---- */
char* engine_state_json(void); /* counters, settings, scanning status */
char* engine_nearby_json(void); /* echoids out near you */
char* engine_team_json(void);
char* engine_pac_json(uint32_t start, uint32_t count);
char* engine_echoid_json(int where, uint32_t index); /* where: 0 team, 1 PAC, 2 nearby */
char* engine_inventory_json(void); /* items, shards, recipes */
char* engine_battle_json(void);
char* engine_take_events_json(void); /* messages since the last call */
char* engine_dungeon_json(void);
char* engine_shards_json(void); /* shards found and devices recharging */

/* ---- Actions (JSON {"ok":bool,"msg":"..."}, caller frees) ---- */
char* engine_battle_begin(uint32_t nearby_index);
char* engine_battle_move(uint8_t slot);
char* engine_battle_switch(uint8_t team_index);
char* engine_battle_ball(uint8_t ball);
char* engine_battle_run(void);
char* engine_battle_close(void);
/* A scanned card (kind: CardKind) starts a battle with its echoid. */
char* engine_card_battle(uint8_t kind, const uint8_t* uid, uint32_t uid_len);
char* engine_craft(uint32_t recipe);
char* engine_use_item(uint8_t item, uint8_t team_index);
char* engine_set_lead(uint8_t team_index);
char* engine_team_to_pac(uint8_t team_index);
char* engine_pac_to_team(uint32_t pac_index);
char* engine_swap(uint32_t pac_index, uint8_t team_index);
char* engine_set_bluetooth(bool on);
char* engine_reset(void);

/* ---- Dungeons (see src/session.h) ---- */
char* engine_jack_in_check(uint32_t nearby_index);
/* The phone is on this WiFi network (its BSSID and name). */
void engine_link_up(const uint8_t mac[6], const char* ssid);
void engine_link_down(void);
char* engine_jack_in(uint32_t nearby_index);
/* Jacks in to the network the phone is connected to. */
char* engine_jack_in_linked(void);
char* engine_dungeon_room(void);
char* engine_dungeon_next(void);
char* engine_dungeon_leave(void);
