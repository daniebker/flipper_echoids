/*
 * Echoids session: one game being played, with no UI.
 *
 * This is the only place the session rules live: the save and starter,
 * scanning visits, the nearby list and time of day, shard
 * claims, battles and what follows them (catching, storing, wins, XP results),
 * items, crafting, team and PAC moves, HP regeneration and autosave. The
 * Flipper app (echoids.c) and the Android app (android/.../cpp) both drive a
 * Session and only draw it: neither makes game decisions of its own.
 *
 * Actions return true when they happened. Either way, s->msg may hold a short
 * message for the player (it fits the Flipper's 128 px screen). Things that
 * happen on their own (spotting an echoid, a shard) are queued as events.
 *
 * Not thread safe: the caller serialises calls.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "game.h"
#include "save.h"
#include "scanner.h"

#define SESSION_NEARBY_MAX   24
#define SESSION_SPOTTED_MAX  128
#define SESSION_ASLEEP_MAX   64
#define SESSION_NOTED_MAX    48
#define SESSION_RESULTS_MAX  24
#define SESSION_RESULT_LEN   32
#define SESSION_EVENTS_MAX   8
#define SESSION_MSG_LEN      40
#define SESSION_REGEN_MS     5000
#define SESSION_AUTOSAVE_MS  20000

typedef enum {
    NearbyWild,
    NearbyOwned,
    NearbyDone, /* defeated or fled this visit */
} NearbyState;

typedef struct {
    uint8_t mac[6];
    int8_t rssi;
    uint8_t state; /* NearbyState */
    char ssid[NAME_LEN];
} Nearby;

typedef enum {
    SessionEventSpotted, /* a wild echoid is out nearby (mac) */
    SessionEventShard, /* a shard was collected (shard) */
    SessionEventRecharge, /* a device's shard is still recharging (shard) */
} SessionEventKind;

typedef struct {
    uint8_t kind; /* SessionEventKind */
    uint8_t mac[6];
    Shard shard;
    char text[SESSION_MSG_LEN];
} SessionEvent;

/* What the UI may offer for an item used on a team member. */
typedef enum {
    ItemUseNone, /* balls: used in battle */
    ItemUseHeal,
    ItemUseRevive,
    ItemUseXp,
} ItemUse;

typedef struct {
    SaveData save;
    bool dirty;
    uint32_t last_save, last_regen;
    uint8_t device_mac[6]; /* the starter comes from this */
    char device_name[NAME_LEN];

    /* Scanning visit */
    bool scanning;
    Nearby nearby[SESSION_NEARBY_MAX];
    uint8_t nearby_count;
    uint8_t nearby_next_replace;
    uint32_t spotted[SESSION_SPOTTED_MAX];
    uint16_t spotted_count; /* echoids seen since the app started */
    uint16_t shards_this_run;
    uint32_t recharge_noted[SESSION_NOTED_MAX];
    uint8_t recharge_noted_count;

    /* Time of day */
    uint8_t hour; /* local hour, 0-23 */
    uint8_t hour_checked;
    uint32_t asleep[SESSION_ASLEEP_MAX];
    uint16_t asleep_count; /* access points heard whose echoids are asleep now */

    /* Battle */
    bool in_battle;
    bool must_switch; /* the active echoid fainted: pick a replacement */
    int8_t battle_nearby;
    Battle battle;
    char results[SESSION_RESULTS_MAX][SESSION_RESULT_LEN];
    uint8_t result_count;

    SessionEvent events[SESSION_EVENTS_MAX];
    uint8_t event_count;

    char msg[SESSION_MSG_LEN];
} Session;

/* Loads the save, or starts a new game with a starter born from the device's
 * MAC. Returns true if a new game was started. */
bool session_init(Session* s, const uint8_t device_mac[6], const char* device_name);
/* Call often (every 50-250 ms): time of day, regen, autosave. */
void session_tick(Session* s);
void session_save(Session* s);
/* Starts over with a new starter. Settings (board setup, Bluetooth) stay. */
bool session_reset(Session* s);

/* ---- Scanning ---- */
void session_scan_start(Session* s);
void session_scan_stop(Session* s);
/* A device heard by the radios (ignored unless scanning). */
void session_sighting(Session* s, const ScanEvent* ev);
bool session_take_event(Session* s, SessionEvent* out);
void session_set_bluetooth(Session* s, bool on);

/* ---- Queries ---- */
Echoid* session_lead(Session* s);
bool session_owns(Session* s, const uint8_t mac[6]);
bool session_pac_has_room(const Session* s);
bool session_team_full(const Session* s);
/* A nearby echoid as it would battle (at a level near your lead's). */
void session_nearby_echoid(Session* s, uint8_t index, Echoid* out);
/* Balls can be thrown in this battle; if not, why (else NULL). */
const char* session_catch_blocked(Session* s);
ItemUse session_item_use(uint8_t item);
/* "Have" count for a recipe's output (PACs for the PAC recipe). */
uint32_t session_recipe_have(const Session* s, size_t recipe);

/* ---- Battle ---- */
bool session_battle_begin(Session* s, uint8_t nearby_index);
bool session_battle_move(Session* s, uint8_t slot);
/* Can this team member be sent in now? */
bool session_battle_can_switch_to(Session* s, uint8_t team_index);
/* Is there anyone else who could fight? */
bool session_battle_others_can_fight(Session* s);
bool session_battle_switch(Session* s, uint8_t team_index);
bool session_battle_ball(Session* s, uint8_t ball);
bool session_battle_run(Session* s);
/* Leaves a finished battle. */
bool session_battle_close(Session* s);

/* ---- Items, crafting, team and PAC ---- */
bool session_craft(Session* s, size_t recipe);
bool session_use_item(Session* s, uint8_t item, uint8_t team_index);
bool session_set_lead(Session* s, uint8_t team_index);
bool session_team_to_pac(Session* s, uint8_t team_index);
bool session_pac_to_team(Session* s, uint32_t pac_index);
/* The PAC echoid joins the team in team_index's place, which goes to the PAC. */
bool session_swap(Session* s, uint32_t pac_index, uint8_t team_index);
