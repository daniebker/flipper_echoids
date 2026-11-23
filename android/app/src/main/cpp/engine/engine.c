/*
 * The Android app's view of the game: JSON in and out of the shared session
 * (src/session.c), which holds every game rule. Nothing here decides anything
 * about the game; it only turns session state into JSON for the Java UI and
 * passes the UI's actions on. See engine.h.
 */
#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <furi.h>

#include "json.h"
#include "pac_store.h"
#include "session.h"

static Session S;
static bool ready;
static char radio_error[48];

/* An action's outcome, with the session's message. */
static char* result(bool ok) {
    Json j;
    json_init(&j);
    json_obj(&j);
    json_kbool(&j, "ok", ok);
    json_kstr(&j, "msg", S.msg);
    json_end_obj(&j);
    S.msg[0] = '\0';
    return json_finish(&j);
}

static const char* nearby_state(uint8_t state) {
    return state == NearbyOwned ? "owned" : (state == NearbyDone ? "fought" : "wild");
}

bool engine_init(const uint8_t mac[6], const char* device_name) {
    if(ready) return false;
    ready = true;
    return session_init(&S, mac, device_name);
}

void engine_tick(void) {
    if(ready) session_tick(&S);
}

void engine_save(void) {
    if(ready) session_save(&S);
}

void engine_unload(void) {
    if(ready) session_save(&S);
    ready = false;
}

void engine_scan_set(bool on) {
    if(on && !S.scanning)
        session_scan_start(&S);
    else if(!on && S.scanning)
        session_scan_stop(&S);
}

int engine_radios_wanted(void) {
    if(!ready || !S.scanning) return 0;
    return S.save.bt_enabled ? 2 : 1;
}

void engine_push_sighting(const ScanEvent* ev) {
    if(ready) session_sighting(&S, ev);
}

void engine_set_radio_error(const char* error) {
    snprintf(radio_error, sizeof(radio_error), "%s", error ? error : "");
}

/* ------------------------------------------------------------------------- */
/* JSON views                                                                */
/* ------------------------------------------------------------------------- */

static void mac_hex(const uint8_t mac[6], char out[18]) {
    snprintf(
        out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void put_sprite(Json* j, const uint8_t mac[6]) {
    uint16_t rows[SPRITE_SIZE];
    echo_sprite(mac, rows);
    json_key(j, "sprite");
    json_arr(j);
    for(int i = 0; i < SPRITE_SIZE; i++)
        json_int(j, rows[i]);
    json_end_arr(j);
}

/* Fields shared by every echoid view. */
static void put_echoid(Json* j, const Echoid* e) {
    EchoGenes g;
    echo_genes(e->mac, &g);
    EchoStats s;
    echo_stats(e, &s);
    EchoRating r;
    echo_rating(e->mac, &r);
    char name[ECHO_NAME_LEN], mac[18];
    echo_name(e->mac, name, sizeof(name));
    mac_hex(e->mac, mac);
    json_kstr(j, "mac", mac);
    json_kstr(j, "name", name);
    json_kstr(j, "species", echo_species_name(e->mac));
    json_kint(j, "type", g.type);
    json_kstr(j, "typeName", echo_type_name(g.type));
    json_kint(j, "rarity", g.rarity);
    json_kstr(j, "rarityName", echo_rarity_name(g.rarity));
    json_kint(j, "level", e->level);
    json_kint(j, "hp", e->hp);
    json_kint(j, "maxHp", s.max_hp);
    json_kint(j, "stars", r.overall);
    put_sprite(j, e->mac);
}

static void put_moves(Json* j, const Echoid* e, const Echoid* against) {
    Move m[MAX_MOVES];
    uint8_t n = echo_moves(e, m);
    EchoGenes ga;
    if(against) echo_genes(against->mac, &ga);
    json_key(j, "moves");
    json_arr(j);
    for(uint8_t i = 0; i < n; i++) {
        json_obj(j);
        json_kstr(j, "name", m[i].name);
        json_kint(j, "type", m[i].type);
        json_kstr(j, "typeName", echo_type_name(m[i].type));
        json_kint(j, "power", m[i].power);
        json_kint(j, "accuracy", m[i].accuracy);
        if(against) json_kint(j, "mult", move_mult(&m[i], ga.type));
        json_end_obj(j);
    }
    json_end_arr(j);
}

/* Everything the details screen shows. */
static void put_details(Json* j, const Echoid* e) {
    put_echoid(j, e);
    EchoGenes g;
    echo_genes(e->mac, &g);
    EchoStats s;
    echo_stats(e, &s);
    json_kint(j, "xp", e->xp);
    json_kint(j, "xpNext", echo_xp_to_next(e->level));
    json_kint(j, "maxLevel", MAX_LEVEL);
    json_kint(j, "atk", s.atk);
    json_kint(j, "def", s.def);
    json_kint(j, "spd", s.spd);
    json_kstr(j, "origin", e->origin);
    EchoRating r;
    echo_rating(e->mac, &r);
    json_key(j, "rating");
    json_obj(j);
    json_kint(j, "overall", r.overall);
    json_kint(j, "total", r.total_stars);
    json_kint(j, "totalMax", STAT_COUNT * 3);
    json_key(j, "stats");
    json_arr(j);
    for(uint8_t i = 0; i < STAT_COUNT; i++) {
        json_obj(j);
        json_kstr(j, "name", stat_name(i));
        json_kint(j, "roll", r.roll[i]);
        json_kint(j, "max", r.max[i]);
        json_kint(j, "stars", r.stars[i]);
        json_end_obj(j);
    }
    json_end_arr(j);
    json_end_obj(j);
    put_moves(j, e, NULL);
    uint8_t at = 0;
    const Move* next = echo_next_move(e, &at);
    json_key(j, "nextMove");
    if(next) {
        json_obj(j);
        json_kstr(j, "name", next->name);
        json_kstr(j, "typeName", echo_type_name(next->type));
        json_kint(j, "level", at);
        json_end_obj(j);
    } else {
        json_null(j);
    }
    uint8_t cant[2];
    echo_unlearnable_types(g.type, cant);
    json_key(j, "cantLearn");
    json_arr(j);
    json_str(j, echo_type_name(cant[0]));
    json_str(j, echo_type_name(cant[1]));
    json_end_arr(j);
    json_kint(j, "activeHours", (long long)echo_active_hours(e->mac));
}

char* engine_state_json(void) {
    Json j;
    json_init(&j);
    json_obj(&j);
    json_kint(&j, "hour", S.hour);
    json_kbool(&j, "scanning", S.scanning);
    json_kbool(&j, "bluetooth", S.save.bt_enabled);
    json_kstr(&j, "error", radio_error);
    json_kint(&j, "spotted", S.spotted_count);
    json_kint(&j, "shards", S.shards_this_run);
    json_kint(&j, "asleep", S.asleep_count);
    json_kint(&j, "nearby", S.nearby_count);
    json_kint(&j, "team", S.save.team_count);
    json_kint(&j, "teamMax", PARTY_MAX);
    json_kbool(&j, "teamFull", session_team_full(&S));
    json_kint(&j, "lead", S.save.lead);
    json_kint(&j, "pac", S.save.pac_count);
    json_kint(&j, "pacCapacity", save_pac_capacity(&S.save));
    json_kint(&j, "pacs", S.save.pacs);
    json_kint(&j, "captures", S.save.captures);
    json_kint(&j, "wins", S.save.wins);
    json_kint(&j, "shardsFound", S.save.shards_found);
    json_kint(&j, "shardTotal", inventory_shard_total(&S.save.inv));
    json_kbool(&j, "inBattle", S.in_battle);
    json_kbool(&j, "inDungeon", session_in_dungeon(&S));
    json_kbool(&j, "wantsLink", session_wants_link(&S));
    json_kint(&j, "dungeonsCleared", S.save.dungeons_cleared);
    json_kint(&j, "teamLevel", session_team_level(&S));
    json_end_obj(&j);
    return json_finish(&j);
}

char* engine_nearby_json(void) {
    Json j;
    json_init(&j);
    json_arr(&j);
    for(uint8_t i = 0; i < S.nearby_count; i++) {
        Echoid wild;
        session_nearby_echoid(&S, i, &wild);
        json_obj(&j);
        put_echoid(&j, &wild);
        json_kint(&j, "index", i);
        json_kint(&j, "rssi", S.nearby[i].rssi);
        json_kstr(&j, "ssid", S.nearby[i].ssid);
        json_kstr(&j, "state", nearby_state(S.nearby[i].state));
        json_end_obj(&j);
    }
    json_end_arr(&j);
    return json_finish(&j);
}

char* engine_team_json(void) {
    Json j;
    json_init(&j);
    json_arr(&j);
    for(uint8_t i = 0; i < S.save.team_count; i++) {
        json_obj(&j);
        put_echoid(&j, &S.save.team[i]);
        json_kint(&j, "index", i);
        json_kbool(&j, "lead", i == S.save.lead);
        json_end_obj(&j);
    }
    json_end_arr(&j);
    return json_finish(&j);
}

char* engine_pac_json(uint32_t start, uint32_t count) {
    Json j;
    json_init(&j);
    json_arr(&j);
    Echoid page[16];
    while(count > 0 && start < S.save.pac_count) {
        uint8_t want = (uint8_t)(count < 16 ? count : 16);
        if(start + want > S.save.pac_count) want = (uint8_t)(S.save.pac_count - start);
        uint8_t got = pac_read_page(start, page, want);
        if(got == 0) break;
        for(uint8_t i = 0; i < got; i++) {
            json_obj(&j);
            put_echoid(&j, &page[i]);
            json_kint(&j, "index", start + i);
            json_end_obj(&j);
        }
        start += got;
        count -= got;
    }
    json_end_arr(&j);
    return json_finish(&j);
}

char* engine_echoid_json(int where, uint32_t index) {
    Echoid e;
    const char* place = "team";
    if(where == 0) {
        if(index >= S.save.team_count) return NULL;
        e = S.save.team[index];
    } else if(where == 1) {
        if(index >= S.save.pac_count || !pac_read(index, &e)) return NULL;
        place = "pac";
    } else {
        if(index >= S.nearby_count) return NULL;
        session_nearby_echoid(&S, (uint8_t)index, &e);
        place = "wild";
    }
    Json j;
    json_init(&j);
    json_obj(&j);
    put_details(&j, &e);
    json_kstr(&j, "place", place);
    json_kint(&j, "index", index);
    if(where == 0) json_kbool(&j, "lead", index == S.save.lead);
    if(where == 2) {
        json_kint(&j, "rssi", S.nearby[index].rssi);
        json_kstr(&j, "ssid", S.nearby[index].ssid);
        json_kstr(&j, "state", nearby_state(S.nearby[index].state));
    }
    json_end_obj(&j);
    return json_finish(&j);
}

char* engine_inventory_json(void) {
    Inventory* inv = &S.save.inv;
    Json j;
    json_init(&j);
    json_obj(&j);
    json_key(&j, "items");
    json_arr(&j);
    static const char* const uses[] = {"ball", "heal", "revive", "xp"};
    for(uint8_t i = 0; i < ItemCount; i++) {
        json_obj(&j);
        json_kint(&j, "id", i);
        json_kstr(&j, "name", item_name(i));
        json_kint(&j, "count", inv->items[i]);
        ItemUse use = session_item_use(i);
        json_kstr(&j, "kind", uses[use]);
        uint16_t heal = item_heal_amount(i);
        if(use == ItemUseHeal) json_kint(&j, "heal", heal == ITEM_HEAL_FULL ? -1 : heal);
        if(use == ItemUseNone) json_kint(&j, "catchX10", ball_mult_x10(i));
        json_end_obj(&j);
    }
    json_end_arr(&j);

    json_key(&j, "shards");
    json_arr(&j);
    for(uint8_t t = 0; t < ECHO_TYPE_COUNT; t++) {
        json_obj(&j);
        json_kstr(&j, "type", echo_type_name(t));
        json_kint(&j, "typeId", t);
        json_key(&j, "tiers");
        json_arr(&j);
        for(uint8_t k = 0; k < SHARD_TIER_COUNT; k++)
            json_int(&j, inv->shards[t][k]);
        json_end_arr(&j);
        json_end_obj(&j);
    }
    json_end_arr(&j);
    json_key(&j, "tierNames");
    json_arr(&j);
    for(uint8_t k = 0; k < SHARD_TIER_COUNT; k++)
        json_str(&j, shard_tier_name(k));
    json_end_arr(&j);

    json_key(&j, "recipes");
    json_arr(&j);
    for(size_t i = 0; i < recipe_count; i++) {
        json_obj(&j);
        json_kint(&j, "index", (long long)i);
        json_kstr(&j, "name", recipes[i].name);
        json_kstr(&j, "recipe", recipes[i].recipe);
        json_kbool(&j, "pac", recipes[i].out == CRAFT_OUT_PAC);
        json_kint(&j, "have", session_recipe_have(&S, i));
        json_kbool(&j, "canCraft", craft_can(inv, i));
        json_end_obj(&j);
    }
    json_end_arr(&j);
    json_kint(&j, "pacSize", PAC_SIZE);
    json_end_obj(&j);
    return json_finish(&j);
}

char* engine_battle_json(void) {
    Json j;
    json_init(&j);
    json_obj(&j);
    json_kbool(&j, "active", S.in_battle);
    if(S.in_battle) {
        Battle* b = &S.battle;
        static const char* const outcomes[] = {"ongoing", "won", "lost", "caught", "fled"};
        json_kstr(&j, "result", outcomes[b->result % 5]);
        json_kbool(&j, "mustSwitch", S.must_switch);
        json_key(&j, "wild");
        json_obj(&j);
        put_echoid(&j, &b->wild);
        json_kstr(&j, "origin", b->wild.origin);
        json_end_obj(&j);
        json_key(&j, "mine");
        json_obj(&j);
        put_echoid(&j, b->mine);
        json_kint(&j, "index", (long long)(b->mine - S.save.team));
        json_end_obj(&j);
        put_moves(&j, b->mine, &b->wild);
        json_key(&j, "log");
        json_arr(&j);
        for(int i = 0; i < BATTLE_LOG_LINES; i++)
            if(b->log[i][0]) json_str(&j, b->log[i]);
        json_end_arr(&j);
        json_kbool(&j, "inDungeon", S.battle_in_dungeon);
        json_kbool(&j, "fromCard", S.battle_card);
        json_kbool(&j, "canRun", !S.battle_in_dungeon);
        const char* blocked = session_catch_blocked(&S);
        json_kbool(&j, "canCatch", blocked == NULL);
        json_kstr(&j, "catchBlocked", blocked ? blocked : "");
        json_key(&j, "balls");
        json_arr(&j);
        for(uint8_t i = 0; i < ItemCount; i++) {
            if(session_item_use(i) != ItemUseNone) continue;
            json_obj(&j);
            json_kint(&j, "id", i);
            json_kstr(&j, "name", item_name(i));
            json_kint(&j, "count", S.save.inv.items[i]);
            json_kint(&j, "chance", echo_capture_chance(&b->wild, i));
            json_end_obj(&j);
        }
        json_end_arr(&j);
        json_kint(&j, "xp", b->xp_gained);
        json_key(&j, "results");
        json_arr(&j);
        for(uint8_t i = 0; i < S.result_count; i++)
            json_str(&j, S.results[i]);
        json_end_arr(&j);
    }
    json_end_obj(&j);
    return json_finish(&j);
}

char* engine_take_events_json(void) {
    Json j;
    json_init(&j);
    json_arr(&j);
    SessionEvent ev;
    while(session_take_event(&S, &ev)) {
        const char* kind = ev.kind == SessionEventSpotted  ? "spotted" :
                           ev.kind == SessionEventEjected  ? "ejected" :
                           ev.shard.tier == ShardPrismatic ? "shard_rare" :
                                                             "shard";
        json_obj(&j);
        json_kstr(&j, "kind", kind);
        json_kstr(&j, "text", ev.text);
        json_end_obj(&j);
    }
    json_end_arr(&j);
    return json_finish(&j);
}

/* ------------------------------------------------------------------------- */
/* Actions: straight to the session                                          */
/* ------------------------------------------------------------------------- */

char* engine_battle_begin(uint32_t i) {
    return result(i < SESSION_NEARBY_MAX && session_battle_begin(&S, (uint8_t)i));
}
char* engine_card_battle(uint8_t kind, const uint8_t* uid, uint32_t uid_len) {
    return result(uid_len <= CARD_UID_MAX && session_card_battle(&S, kind, uid, (uint8_t)uid_len));
}
char* engine_battle_move(uint8_t slot) {
    return result(session_battle_move(&S, slot));
}
char* engine_battle_switch(uint8_t i) {
    return result(session_battle_switch(&S, i));
}
char* engine_battle_ball(uint8_t ball) {
    return result(session_battle_ball(&S, ball));
}
char* engine_battle_run(void) {
    return result(session_battle_run(&S));
}
char* engine_battle_close(void) {
    return result(session_battle_close(&S));
}
char* engine_craft(uint32_t recipe) {
    return result(session_craft(&S, recipe));
}
char* engine_use_item(uint8_t item, uint8_t i) {
    return result(session_use_item(&S, item, i));
}
char* engine_set_lead(uint8_t i) {
    return result(session_set_lead(&S, i));
}
char* engine_team_to_pac(uint8_t i) {
    return result(session_team_to_pac(&S, i));
}
char* engine_pac_to_team(uint32_t i) {
    return result(session_pac_to_team(&S, i));
}
char* engine_swap(uint32_t pac_index, uint8_t team_index) {
    return result(session_swap(&S, pac_index, team_index));
}
char* engine_set_bluetooth(bool on) {
    session_set_bluetooth(&S, on);
    return result(true);
}
char* engine_reset(void) {
    return result(session_reset(&S));
}

/* ------------------------------------------------------------------------- */
/* Dungeons                                                                  */
/* ------------------------------------------------------------------------- */

char* engine_dungeon_json(void) {
    static const char* const kinds[] = {"fight", "cache", "rest", "boss"};
    const Dungeon* d = &S.dungeon;
    Json j;
    json_init(&j);
    json_obj(&j);
    json_kbool(&j, "active", d->active);
    if(d->active) {
        char mac[18];
        mac_hex(d->mac, mac);
        json_kstr(&j, "name", d->name);
        json_kstr(&j, "ssid", d->ssid);
        json_kstr(&j, "mac", mac);
        json_kint(&j, "type", d->map.type);
        json_kstr(&j, "typeName", echo_type_name(d->map.type));
        json_kint(&j, "room", d->room);
        json_kbool(&j, "roomDone", d->room_done);
        json_kbool(&j, "cleared", d->cleared);
        json_kint(&j, "level", dungeon_foe_level(session_team_level(&S), false));
        json_kint(&j, "bossLevel", dungeon_foe_level(session_team_level(&S), true));
        json_key(&j, "rooms");
        json_arr(&j);
        for(uint8_t r = 0; r < d->map.rooms; r++) {
            json_obj(&j);
            json_kstr(&j, "kind", kinds[d->map.kind[r] % 4]);
            json_kbool(&j, "done", r < d->room || (r == d->room && d->room_done));
            json_kbool(&j, "current", r == d->room);
            Echoid foe;
            if(session_dungeon_foe(&S, r, &foe)) {
                json_key(&j, "foe");
                json_obj(&j);
                put_echoid(&j, &foe);
                json_end_obj(&j);
            }
            json_end_obj(&j);
        }
        json_end_arr(&j);
    } else {
        /* The network the phone is on, and the dungeon it hides. */
        char name[NAME_LEN + 12];
        DungeonLayout map;
        json_kbool(&j, "linked", session_linked_dungeon(&S, name, sizeof(name), &map));
        if(S.link_up) {
            char mac[18];
            mac_hex(S.link_mac, mac);
            json_kstr(&j, "ssid", S.link_ssid);
            json_kstr(&j, "mac", mac);
            json_kstr(&j, "name", name);
            json_kint(&j, "type", map.type);
            json_kstr(&j, "typeName", echo_type_name(map.type));
            json_kint(&j, "roomCount", map.rooms);
            json_kint(&j, "level", dungeon_foe_level(session_team_level(&S), false));
            json_kint(&j, "bossLevel", dungeon_foe_level(session_team_level(&S), true));
            json_key(&j, "boss");
            json_obj(&j);
            Echoid boss;
            echo_init(&boss, map.foe[map.rooms - 1], dungeon_foe_level(session_team_level(&S), true), S.link_ssid);
            put_echoid(&j, &boss);
            json_end_obj(&j);
        }
    }
    json_end_obj(&j);
    return json_finish(&j);
}

char* engine_jack_in_check(uint32_t i) {
    return result(i < SESSION_NEARBY_MAX && session_jack_in_check(&S, (uint8_t)i));
}
void engine_link_up(const uint8_t mac[6], const char* ssid) {
    if(ready) session_link_named(&S, mac, ssid);
}
void engine_link_down(void) {
    if(ready) session_link_down(&S);
}
char* engine_jack_in_linked(void) {
    return result(session_jack_in_linked(&S));
}
char* engine_jack_in(uint32_t i) {
    return result(i < SESSION_NEARBY_MAX && session_jack_in(&S, (uint8_t)i));
}
char* engine_dungeon_room(void) {
    return result(session_dungeon_room(&S));
}
char* engine_dungeon_next(void) {
    return result(session_dungeon_next(&S));
}
char* engine_dungeon_leave(void) {
    return result(session_dungeon_leave(&S));
}

/* The shard log: devices heard this outing, newest first. */
char* engine_shards_json(void) {
    Json j;
    json_init(&j);
    json_obj(&j);
    json_kint(&j, "collected", S.shards_this_run);
    uint8_t cooling = 0;
    for(uint8_t i = 0; i < S.shard_log_count; i++)
        if(session_shard_left(&S, i)) cooling++;
    json_kint(&j, "cooling", cooling);
    json_key(&j, "cooldowns");
    json_arr(&j);
    for(uint8_t k = 0; k < SHARD_TIER_COUNT; k++) {
        char d[16];
        format_duration(shard_cooldown_s(k), d, sizeof(d));
        json_obj(&j);
        json_kstr(&j, "tier", shard_tier_name(k));
        json_kstr(&j, "time", d);
        json_end_obj(&j);
    }
    json_end_arr(&j);
    json_key(&j, "devices");
    json_arr(&j);
    for(uint8_t i = 0; i < S.shard_log_count; i++) {
        const ShardSeen* e = &S.shard_log[i];
        uint32_t left = session_shard_left(&S, i);
        char d[16];
        format_duration(left, d, sizeof(d));
        json_obj(&j);
        json_kstr(&j, "name", e->name);
        json_kint(&j, "typeId", e->shard.type);
        json_kstr(&j, "type", echo_type_name(e->shard.type));
        json_kint(&j, "tier", e->shard.tier);
        json_kstr(&j, "tierName", shard_tier_name(e->shard.tier));
        json_kbool(&j, "collected", e->collected);
        json_kint(&j, "left", left);
        json_kstr(&j, "leftText", left ? d : "");
        json_end_obj(&j);
    }
    json_end_arr(&j);
    json_end_obj(&j);
    return json_finish(&j);
}
