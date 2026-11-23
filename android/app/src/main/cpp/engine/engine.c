/*
 * Echoids game engine for the Android app. See engine.h.
 *
 * The rules come from src/game.c; the session behaviour here (nearby list,
 * time of day, shard cooldowns, catching, XP sharing, items, team and PAC)
 * mirrors src/echoids.c so the phone and the Flipper play the same.
 */
#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <furi.h>
#include <furi_hal.h>

#include "json.h"
#include "pac_store.h"
#include "save.h"

#define NEARBY_MAX         24
#define SPOTTED_MAX        128
#define ASLEEP_MAX         64
#define RECHARGE_NOTED_MAX 48
#define SIGHTING_QUEUE     512
#define EVENTS_MAX         16
#define REGEN_MS           5000
#define AUTOSAVE_MS        20000
#define DEMO_INTERVAL_MS   1800
#define DEMO_POOL          24
#define RESULT_LINES_MAX   24

typedef enum {
    NearbyWild,
    NearbyOwned,
    NearbyDone, /* defeated or fled this visit */
} NearbyState;

typedef struct {
    uint8_t mac[6];
    int8_t rssi;
    uint8_t state;
    char ssid[NAME_LEN];
} Nearby;

typedef struct {
    char kind[12];
    char text[48];
} Event;

static struct {
    bool ready;
    SaveData save;
    bool dirty;
    uint32_t last_save, last_regen;

    /* Scanning */
    bool scanning;
    uint32_t last_demo;
    char radio_error[48];
    ScanEvent queue[SIGHTING_QUEUE];
    int q_head, q_count;

    /* Nearby echoids and this visit's counters */
    Nearby nearby[NEARBY_MAX];
    uint8_t nearby_count;
    uint8_t nearby_next_replace;
    uint32_t spotted[SPOTTED_MAX];
    uint16_t spotted_count;
    uint16_t shards_this_run;
    uint32_t recharge_noted[RECHARGE_NOTED_MAX];
    uint8_t recharge_noted_count;

    /* Time of day */
    uint8_t hour, hour_checked;
    uint32_t asleep[ASLEEP_MAX];
    uint16_t asleep_count;

    /* Battle */
    bool in_battle;
    bool must_switch;
    int battle_nearby;
    Battle battle;
    char results[RESULT_LINES_MAX][40];
    uint8_t result_count;

    Event events[EVENTS_MAX];
    uint8_t event_count;
} E;

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static void event(const char* kind, const char* text) {
    if(E.event_count == EVENTS_MAX) {
        memmove(&E.events[0], &E.events[1], sizeof(Event) * (EVENTS_MAX - 1));
        E.event_count--;
    }
    Event* e = &E.events[E.event_count++];
    snprintf(e->kind, sizeof(e->kind), "%s", kind);
    snprintf(e->text, sizeof(e->text), "%s", text);
}

static char* result(bool ok, const char* msg) {
    Json j;
    json_init(&j);
    json_obj(&j);
    json_kbool(&j, "ok", ok);
    json_kstr(&j, "msg", msg ? msg : "");
    json_end_obj(&j);
    return json_finish(&j);
}

static uint8_t current_hour(void) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    return dt.hour % 24;
}

static Echoid* lead(void) {
    return &E.save.team[E.save.lead];
}

static bool owns(const uint8_t mac[6]) {
    for(uint8_t i = 0; i < E.save.team_count; i++)
        if(memcmp(E.save.team[i].mac, mac, 6) == 0) return true;
    return pac_find(mac, E.save.pac_count) != PAC_NOT_FOUND;
}

static bool pac_has_room(void) {
    return E.save.pac_count < save_pac_capacity(&E.save);
}

static void name_of(const uint8_t mac[6], char* out, size_t size) {
    echo_name(mac, out, size);
}

static void new_game(const uint8_t mac[6], const char* device_name) {
    SaveData* sv = &E.save;
    memset(sv, 0, sizeof(*sv));
    sv->magic = SAVE_MAGIC;
    sv->version = SAVE_VERSION;
    sv->source = ScanSourceBoard; /* on a phone: its own radios */
    sv->bt_enabled = 1;
    echo_init(&sv->team[0], mac, 1, device_name ? device_name : "Phone");
    sv->team_count = 1;
    sv->lead = 0;
    sv->pacs = 1;
    sv->pac_count = 0;
    pac_clear();
    E.dirty = true;
}

void engine_save(void) {
    if(!E.ready) return;
    if(save_store(&E.save)) E.dirty = false;
    E.last_save = furi_get_tick();
}

void engine_unload(void) {
    if(E.ready) engine_save();
    E.ready = false;
}

bool engine_init(const uint8_t mac[6], const char* device_name) {
    if(E.ready) return false;
    memset(&E, 0, sizeof(E));
    game_rng_seed(furi_hal_random_get());
    bool fresh = !save_load(&E.save);
    if(fresh) new_game(mac, device_name);
    E.ready = true;
    E.battle_nearby = -1;
    E.hour = E.hour_checked = current_hour();
    E.last_regen = E.last_save = furi_get_tick();
    if(fresh) engine_save();
    return fresh;
}

/* ------------------------------------------------------------------------- */
/* Sightings                                                                 */
/* ------------------------------------------------------------------------- */

static void note_asleep(const uint8_t mac[6]) {
    uint32_t h = echo_hash_mac(mac);
    uint16_t known = E.asleep_count < ASLEEP_MAX ? E.asleep_count : ASLEEP_MAX;
    for(uint16_t i = 0; i < known; i++)
        if(E.asleep[i] == h) return;
    if(E.asleep_count < ASLEEP_MAX) E.asleep[E.asleep_count] = h;
    if(E.asleep_count < UINT16_MAX) E.asleep_count++;
}

static void nearby_prune_hour(void) {
    uint8_t keep = 0;
    for(uint8_t i = 0; i < E.nearby_count; i++) {
        if(echo_active_at(E.nearby[i].mac, E.hour))
            E.nearby[keep++] = E.nearby[i];
        else
            note_asleep(E.nearby[i].mac);
    }
    E.nearby_count = keep;
    E.nearby_next_replace = 0;
}

static void handle_ap(const ScanEvent* ev) {
    if(!echo_active_at(ev->mac, E.hour)) {
        note_asleep(ev->mac);
        return;
    }
    for(uint8_t i = 0; i < E.nearby_count; i++) {
        if(memcmp(E.nearby[i].mac, ev->mac, 6) == 0) {
            E.nearby[i].rssi = ev->rssi;
            return;
        }
    }
    Nearby* n;
    if(E.nearby_count < NEARBY_MAX) {
        n = &E.nearby[E.nearby_count++];
    } else {
        /* Recycle slots round-robin, but never the one in battle. */
        uint8_t slot = E.nearby_next_replace;
        if(E.in_battle && (int)slot == E.battle_nearby) slot = (slot + 1) % NEARBY_MAX;
        E.nearby_next_replace = (slot + 1) % NEARBY_MAX;
        n = &E.nearby[slot];
    }
    memcpy(n->mac, ev->mac, 6);
    n->rssi = ev->rssi;
    snprintf(n->ssid, sizeof(n->ssid), "%s", ev->name[0] ? ev->name : "<hidden>");
    n->state = owns(ev->mac) ? NearbyOwned : NearbyWild;

    /* Count each echoid once per visit, even if its slot was recycled. */
    uint32_t h = echo_hash_mac(ev->mac);
    bool counted = false;
    uint16_t known = E.spotted_count < SPOTTED_MAX ? E.spotted_count : SPOTTED_MAX;
    for(uint16_t i = 0; i < known && !counted; i++)
        counted = E.spotted[i] == h;
    if(!counted) {
        if(E.spotted_count < SPOTTED_MAX) E.spotted[E.spotted_count] = h;
        if(E.spotted_count < UINT16_MAX) E.spotted_count++;
        if(n->state == NearbyWild) {
            char name[ECHO_NAME_LEN], msg[48];
            name_of(ev->mac, name, sizeof(name));
            snprintf(msg, sizeof(msg), "A wild %s is nearby", name);
            event("spotted", msg);
        }
    }
}

static void handle_bt(const ScanEvent* ev) {
    SaveData* sv = &E.save;
    uint32_t h = echo_hash_mac(ev->mac);
    uint32_t remaining = 0;
    Shard s = shard_from_mac(ev->mac);
    char msg[48];
    if(!shard_claim(
           sv->seen_bt,
           sv->seen_bt_time,
           &sv->seen_bt_count,
           SEEN_BT_MAX,
           h,
           s.tier,
           furi_hal_rtc_get_timestamp(),
           &remaining)) {
        /* Mention a recharging device once per visit. */
        for(uint8_t i = 0; i < E.recharge_noted_count; i++)
            if(E.recharge_noted[i] == h) return;
        if(E.recharge_noted_count < RECHARGE_NOTED_MAX) E.recharge_noted[E.recharge_noted_count++] = h;
        char left[12];
        format_duration(remaining, left, sizeof(left));
        snprintf(
            msg,
            sizeof(msg),
            "%s %s shard recharging: %s left",
            shard_tier_name(s.tier),
            echo_type_name(s.type),
            left);
        event("recharge", msg);
        return;
    }
    if(sv->inv.shards[s.type][s.tier] < 9999) sv->inv.shards[s.type][s.tier]++;
    sv->shards_found++;
    if(E.shards_this_run < UINT16_MAX) E.shards_this_run++;
    E.dirty = true;
    snprintf(msg, sizeof(msg), "+1 %s %s shard", shard_tier_name(s.tier), echo_type_name(s.type));
    event(s.tier == ShardPrismatic ? "shard_rare" : "shard", msg);
}

void engine_push_sighting(const ScanEvent* ev) {
    if(!E.ready || !E.scanning || E.save.source != ScanSourceBoard) return;
    if(E.q_count == SIGHTING_QUEUE) {
        E.q_head = (E.q_head + 1) % SIGHTING_QUEUE;
        E.q_count--;
    }
    E.queue[(E.q_head + E.q_count) % SIGHTING_QUEUE] = *ev;
    E.q_count++;
}

static const char* const demo_names[] = {
    "HomeNet",  "CoffeeBar", "FreeWiFi", "Linksys", "NETGEAR42", "Office-5G",
    "Pixel_7",  "TP-Link",   "Guest",    "Library", "Airport",   "Hotel_Lobby",
};

static bool demo_sighting(ScanEvent* out) {
    uint32_t now = furi_get_tick();
    if(now - E.last_demo < DEMO_INTERVAL_MS) return false;
    E.last_demo = now;
    memset(out, 0, sizeof(*out));
    uint32_t idx = furi_hal_random_get() % DEMO_POOL;
    bool bt = E.save.bt_enabled && furi_hal_random_get() % 3 == 0;
    out->type = bt ? ScanEventBt : ScanEventAp;
    out->mac[0] = bt ? 0xD0 : 0xDE;
    out->mac[1] = 0x00;
    out->mac[2] = 0x5E;
    out->mac[3] = (uint8_t)(idx * 37);
    out->mac[4] = (uint8_t)(idx * 101 + 7);
    out->mac[5] = (uint8_t)idx;
    out->rssi = (int8_t)(-40 - (int)(furi_hal_random_get() % 50));
    if(bt)
        snprintf(out->name, SCAN_NAME_LEN, "BLE-%02X%02X", out->mac[4], out->mac[5]);
    else
        snprintf(out->name, SCAN_NAME_LEN, "%s", demo_names[idx % 12]);
    return true;
}

static void begin_visit(void) {
    E.recharge_noted_count = 0;
    for(uint8_t i = 0; i < E.nearby_count; i++)
        if(E.nearby[i].state == NearbyWild && owns(E.nearby[i].mac)) E.nearby[i].state = NearbyOwned;
}

void engine_scan_set(bool on) {
    if(on && !E.scanning) begin_visit();
    E.scanning = on;
    E.q_count = 0;
    E.last_demo = furi_get_tick();
}

int engine_radios_wanted(void) {
    if(!E.ready || !E.scanning || E.save.source != ScanSourceBoard) return 0;
    return E.save.bt_enabled ? 2 : 1;
}

void engine_set_radio_error(const char* error) {
    snprintf(E.radio_error, sizeof(E.radio_error), "%s", error ? error : "");
}

/* ------------------------------------------------------------------------- */
/* Tick                                                                      */
/* ------------------------------------------------------------------------- */

void engine_tick(void) {
    if(!E.ready) return;
    uint32_t now = furi_get_tick();

    /* A new hour brings different echoids (after any battle). */
    E.hour = current_hour();
    if(E.hour != E.hour_checked && !E.in_battle) {
        E.hour_checked = E.hour;
        E.asleep_count = 0;
        nearby_prune_hour();
    }

    if(E.scanning) {
        ScanEvent ev;
        if(E.save.source == ScanSourceDemo) {
            if(demo_sighting(&ev)) {
                if(ev.type == ScanEventAp)
                    handle_ap(&ev);
                else
                    handle_bt(&ev);
            }
        } else {
            while(E.q_count) {
                ev = E.queue[E.q_head];
                E.q_head = (E.q_head + 1) % SIGHTING_QUEUE;
                E.q_count--;
                if(ev.type == ScanEventAp)
                    handle_ap(&ev);
                else if(E.save.bt_enabled)
                    handle_bt(&ev);
            }
        }
    }

    if(now - E.last_regen >= REGEN_MS) {
        E.last_regen = now;
        if(!E.in_battle) {
            for(uint8_t i = 0; i < E.save.team_count; i++) {
                Echoid* e = &E.save.team[i];
                EchoStats s;
                echo_stats(e, &s);
                /* Fainted echoids stay down until a Revive Crystal. */
                if(e->hp > 0 && e->hp < s.max_hp) e->hp++;
            }
        }
    }

    if(E.dirty && !E.in_battle && now - E.last_save >= AUTOSAVE_MS) engine_save();
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
    name_of(e->mac, name, sizeof(name));
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
        if(against) json_kint(j, "mult", echo_type_mult(m[i].type, ga.type));
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
    json_key(j, "cantLearn");
    json_arr(j);
    json_str(j, echo_type_name((uint8_t)((g.type + 1) % ECHO_TYPE_COUNT)));
    json_str(j, echo_type_name((uint8_t)((g.type + ECHO_TYPE_COUNT - 1) % ECHO_TYPE_COUNT)));
    json_end_arr(j);
    json_kint(j, "activeHours", (long long)echo_active_hours(e->mac));
}

char* engine_state_json(void) {
    Json j;
    json_init(&j);
    json_obj(&j);
    json_kint(&j, "hour", E.hour);
    json_kbool(&j, "scanning", E.scanning);
    json_kint(&j, "source", E.save.source);
    json_kbool(&j, "bluetooth", E.save.bt_enabled);
    json_kstr(&j, "error", E.save.source == ScanSourceBoard ? E.radio_error : "");
    json_kint(&j, "spotted", E.spotted_count);
    json_kint(&j, "shards", E.shards_this_run);
    json_kint(&j, "asleep", E.asleep_count);
    json_kint(&j, "nearby", E.nearby_count);
    json_kint(&j, "team", E.save.team_count);
    json_kint(&j, "teamMax", PARTY_MAX);
    json_kint(&j, "lead", E.save.lead);
    json_kint(&j, "pac", E.save.pac_count);
    json_kint(&j, "pacCapacity", save_pac_capacity(&E.save));
    json_kint(&j, "pacs", E.save.pacs);
    json_kint(&j, "captures", E.save.captures);
    json_kint(&j, "wins", E.save.wins);
    json_kint(&j, "shardsFound", E.save.shards_found);
    json_kint(&j, "shardTotal", inventory_shard_total(&E.save.inv));
    json_kbool(&j, "inBattle", E.in_battle);
    json_end_obj(&j);
    return json_finish(&j);
}

char* engine_nearby_json(void) {
    Json j;
    json_init(&j);
    json_arr(&j);
    for(uint8_t i = 0; i < E.nearby_count; i++) {
        Nearby* n = &E.nearby[i];
        Echoid wild;
        echo_init(&wild, n->mac, echo_wild_level(n->mac, lead()->level), n->ssid);
        json_obj(&j);
        put_echoid(&j, &wild);
        json_kint(&j, "index", i);
        json_kint(&j, "rssi", n->rssi);
        json_kstr(&j, "ssid", n->ssid);
        json_kstr(
            &j,
            "state",
            n->state == NearbyOwned ? "owned" : (n->state == NearbyDone ? "fought" : "wild"));
        json_end_obj(&j);
    }
    json_end_arr(&j);
    return json_finish(&j);
}

char* engine_team_json(void) {
    Json j;
    json_init(&j);
    json_arr(&j);
    for(uint8_t i = 0; i < E.save.team_count; i++) {
        json_obj(&j);
        put_echoid(&j, &E.save.team[i]);
        json_kint(&j, "index", i);
        json_kbool(&j, "lead", i == E.save.lead);
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
    while(count > 0 && start < E.save.pac_count) {
        uint8_t want = (uint8_t)(count < 16 ? count : 16);
        if(start + want > E.save.pac_count) want = (uint8_t)(E.save.pac_count - start);
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
        if(index >= E.save.team_count) return NULL;
        e = E.save.team[index];
    } else if(where == 1) {
        if(index >= E.save.pac_count || !pac_read(index, &e)) return NULL;
        place = "pac";
    } else {
        if(index >= E.nearby_count) return NULL;
        Nearby* n = &E.nearby[index];
        echo_init(&e, n->mac, echo_wild_level(n->mac, lead()->level), n->ssid);
        place = "wild";
    }
    Json j;
    json_init(&j);
    json_obj(&j);
    put_details(&j, &e);
    json_kstr(&j, "place", place);
    json_kint(&j, "index", index);
    if(where == 0) json_kbool(&j, "lead", index == E.save.lead);
    if(where == 2) {
        json_kint(&j, "rssi", E.nearby[index].rssi);
        uint8_t st = E.nearby[index].state;
        json_kstr(&j, "state", st == NearbyOwned ? "owned" : (st == NearbyDone ? "fought" : "wild"));
    }
    json_end_obj(&j);
    return json_finish(&j);
}

char* engine_inventory_json(void) {
    Inventory* inv = &E.save.inv;
    Json j;
    json_init(&j);
    json_obj(&j);
    json_key(&j, "items");
    json_arr(&j);
    for(uint8_t i = 0; i < ItemCount; i++) {
        json_obj(&j);
        json_kint(&j, "id", i);
        json_kstr(&j, "name", item_name(i));
        json_kint(&j, "count", inv->items[i]);
        uint16_t heal = item_heal_amount(i);
        const char* kind = i <= ItemPrismBall ? "ball" :
                           heal               ? "heal" :
                           i == ItemRevive    ? "revive" :
                           i == ItemCrystal   ? "xp" :
                                                "other";
        json_kstr(&j, "kind", kind);
        if(heal) json_kint(&j, "heal", heal == ITEM_HEAL_FULL ? -1 : heal);
        if(i <= ItemPrismBall) json_kint(&j, "catchX10", ball_mult_x10(i));
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
        const Recipe* r = &recipes[i];
        bool pac = r->out == CRAFT_OUT_PAC;
        json_obj(&j);
        json_kint(&j, "index", (long long)i);
        json_kstr(&j, "name", r->name);
        json_kstr(&j, "recipe", r->recipe);
        json_kbool(&j, "pac", pac);
        json_kint(&j, "have", pac ? E.save.pacs : (r->out < ItemCount ? inv->items[r->out] : 0));
        json_kbool(&j, "canCraft", craft_can(inv, i));
        json_end_obj(&j);
    }
    json_end_arr(&j);
    json_kint(&j, "pacSize", PAC_SIZE);
    json_end_obj(&j);
    return json_finish(&j);
}

/* ------------------------------------------------------------------------- */
/* Battle                                                                    */
/* ------------------------------------------------------------------------- */

static Nearby* battle_nearby(void) {
    if(E.battle_nearby < 0 || E.battle_nearby >= E.nearby_count) return NULL;
    Nearby* n = &E.nearby[E.battle_nearby];
    return memcmp(n->mac, E.battle.wild.mac, 6) == 0 ? n : NULL;
}

static bool others_can_fight(void) {
    for(uint8_t i = 0; i < E.save.team_count; i++)
        if(&E.save.team[i] != E.battle.mine && E.save.team[i].hp > 0) return true;
    return false;
}

static void results_add(const char* line) {
    if(E.result_count < RESULT_LINES_MAX)
        snprintf(E.results[E.result_count++], sizeof(E.results[0]), "%s", line);
}

/* After every battle action: captures, wins, fainting, XP results. */
static void after_action(void) {
    Battle* b = &E.battle;
    if(b->result == BattleLost && others_can_fight()) {
        b->result = BattleOngoing;
        E.must_switch = true;
        return;
    }
    if(b->result == BattleOngoing) return;
    Nearby* n = battle_nearby();
    char line[48];

    switch(b->result) {
    case BattleCaptured: {
        SaveData* sv = &E.save;
        Echoid caught = b->wild;
        if(caught.hp == 0) caught.hp = 1;
        caught.flags = 0;
        bool stored = false;
        if(owns(caught.mac)) {
            results_add("Already caught this one!");
        } else if(sv->team_count < PARTY_MAX) {
            sv->team[sv->team_count++] = caught;
            stored = true;
        } else if(pac_has_room() && pac_append(&caught)) {
            sv->pac_count++;
            results_add("Team full: sent to the PAC");
            stored = true;
        } else {
            results_add("Couldn't store it: no room");
        }
        if(stored) sv->captures++;
        if(n && (stored || owns(caught.mac))) n->state = NearbyOwned;
        break;
    }
    case BattleWon:
        E.save.wins++;
        if(n) n->state = NearbyDone;
        break;
    case BattleFled:
        if(n) n->state = NearbyDone;
        break;
    default:
        break;
    }

    /* What everyone who fought got out of it. */
    for(uint8_t i = 0; i < b->fought_n && b->xp_gained; i++) {
        Echoid* e = b->fought[i];
        char name[ECHO_NAME_LEN];
        name_of(e->mac, name, sizeof(name));
        uint8_t before = b->fought_level_before[i];
        if(e->level > before)
            snprintf(
                line,
                sizeof(line),
                "%s +%lu XP, Lv %u > %u",
                name,
                (unsigned long)b->fought_xp[i],
                before,
                e->level);
        else
            snprintf(line, sizeof(line), "%s +%lu XP", name, (unsigned long)b->fought_xp[i]);
        results_add(line);
        for(uint16_t lv = (uint16_t)before + 1; lv <= e->level; lv++) {
            const Move* m = echo_move_learned_at(e->mac, (uint8_t)lv);
            if(m) {
                snprintf(line, sizeof(line), "%s learned %s!", name, m->name);
                results_add(line);
            }
        }
    }
    E.dirty = true;
}

char* engine_battle_begin(uint32_t index) {
    if(E.in_battle) return result(false, "Already in a battle");
    if(index >= E.nearby_count) return result(false, "It's gone");
    Nearby* n = &E.nearby[index];
    if(n->state == NearbyOwned || owns(n->mac)) {
        n->state = NearbyOwned;
        return result(false, "Already caught");
    }
    if(n->state == NearbyDone) return result(false, "Already fought: find others");
    if(!echo_active_at(n->mac, E.hour)) return result(false, "It went to sleep...");
    Echoid* first = lead();
    if(first->hp == 0) {
        first = NULL;
        for(uint8_t i = 0; i < E.save.team_count && !first; i++)
            if(E.save.team[i].hp > 0) first = &E.save.team[i];
    }
    if(!first) return result(false, "Your team has fainted: use Revive Crystals");
    battle_start(&E.battle, first, n->mac, echo_wild_level(n->mac, lead()->level), n->ssid);
    E.battle_nearby = (int)index;
    E.in_battle = true;
    E.must_switch = false;
    E.result_count = 0;
    return result(true, "");
}

char* engine_battle_move(uint8_t slot) {
    if(!E.in_battle || E.battle.result != BattleOngoing || E.must_switch)
        return result(false, "You can't attack now");
    battle_attack(&E.battle, slot);
    after_action();
    return result(true, "");
}

char* engine_battle_switch(uint8_t team_index) {
    if(!E.in_battle || E.battle.result != BattleOngoing) return result(false, "Not in a battle");
    if(team_index >= E.save.team_count) return result(false, "No such team member");
    Echoid* next = &E.save.team[team_index];
    if(next == E.battle.mine) return result(false, "Already fighting");
    if(next->hp == 0) return result(false, "It has fainted");
    battle_switch(&E.battle, next, !E.must_switch);
    E.must_switch = false;
    after_action();
    return result(true, "");
}

char* engine_battle_ball(uint8_t ball) {
    if(!E.in_battle || E.battle.result != BattleOngoing || E.must_switch)
        return result(false, "You can't throw now");
    if(owns(E.battle.wild.mac)) return result(false, "You already have this echoid");
    if(E.save.team_count >= PARTY_MAX && !pac_has_room())
        return result(false, "No room: craft a PAC first");
    if(!battle_throw(&E.battle, &E.save.inv, ball)) return result(false, "You have none of those");
    E.dirty = true;
    after_action();
    return result(true, "");
}

char* engine_battle_run(void) {
    if(!E.in_battle || E.battle.result != BattleOngoing || E.must_switch)
        return result(false, "You can't run now");
    battle_flee(&E.battle);
    after_action();
    return result(true, "");
}

char* engine_battle_close(void) {
    if(!E.in_battle) return result(true, "");
    if(E.battle.result == BattleOngoing) return result(false, "The battle isn't over");
    E.in_battle = false;
    E.battle_nearby = -1;
    engine_save();
    return result(true, "");
}

char* engine_battle_json(void) {
    Json j;
    json_init(&j);
    json_obj(&j);
    json_kbool(&j, "active", E.in_battle);
    if(E.in_battle) {
        Battle* b = &E.battle;
        static const char* const results[] = {"ongoing", "won", "lost", "caught", "fled"};
        json_kstr(&j, "result", results[b->result % 5]);
        json_kbool(&j, "mustSwitch", E.must_switch);
        json_key(&j, "wild");
        json_obj(&j);
        put_echoid(&j, &b->wild);
        json_end_obj(&j);
        json_key(&j, "mine");
        json_obj(&j);
        put_echoid(&j, b->mine);
        json_kint(&j, "index", (long long)(b->mine - E.save.team));
        json_end_obj(&j);
        put_moves(&j, b->mine, &b->wild);
        json_key(&j, "log");
        json_arr(&j);
        for(int i = 0; i < BATTLE_LOG_LINES; i++)
            if(b->log[i][0]) json_str(&j, b->log[i]);
        json_end_arr(&j);
        bool owned = owns(b->wild.mac);
        json_kbool(&j, "canCatch", !owned && (E.save.team_count < PARTY_MAX || pac_has_room()));
        json_kstr(
            &j,
            "catchBlocked",
            owned                                                     ? "You already have this echoid" :
            (E.save.team_count >= PARTY_MAX && !pac_has_room()) ? "No room: craft a PAC first" :
                                                                        "");
        json_key(&j, "balls");
        json_arr(&j);
        for(uint8_t i = 0; i <= ItemPrismBall; i++) {
            json_obj(&j);
            json_kint(&j, "id", i);
            json_kstr(&j, "name", item_name(i));
            json_kint(&j, "count", E.save.inv.items[i]);
            json_kint(&j, "chance", echo_capture_chance(&b->wild, i));
            json_end_obj(&j);
        }
        json_end_arr(&j);
        json_kint(&j, "xp", b->xp_gained);
        json_key(&j, "results");
        json_arr(&j);
        for(uint8_t i = 0; i < E.result_count; i++)
            json_str(&j, E.results[i]);
        json_end_arr(&j);
    }
    json_end_obj(&j);
    return json_finish(&j);
}

/* ------------------------------------------------------------------------- */
/* Items, crafting, team and PAC                                             */
/* ------------------------------------------------------------------------- */

char* engine_craft(uint32_t recipe) {
    if(recipe >= recipe_count) return result(false, "No such recipe");
    if(!craft_do(&E.save.inv, recipe)) return result(false, "Not enough shards");
    char msg[48];
    if(recipes[recipe].out == CRAFT_OUT_PAC) {
        if(E.save.pacs < UINT32_MAX / PAC_SIZE) E.save.pacs++;
        snprintf(
            msg, sizeof(msg), "New PAC! Room for %lu", (unsigned long)save_pac_capacity(&E.save));
    } else {
        snprintf(msg, sizeof(msg), "Crafted %s", recipes[recipe].name);
    }
    E.dirty = true;
    engine_save();
    return result(true, msg);
}

char* engine_use_item(uint8_t item, uint8_t team_index) {
    if(E.in_battle) return result(false, "Not during a battle");
    if(item >= ItemCount || team_index >= E.save.team_count) return result(false, "Can't use that");
    Inventory* inv = &E.save.inv;
    Echoid* t = &E.save.team[team_index];
    EchoStats s;
    echo_stats(t, &s);
    char name[ECHO_NAME_LEN], msg[64];
    name_of(t->mac, name, sizeof(name));
    if(inv->items[item] == 0) return result(false, "You have none");
    uint16_t heal = item_heal_amount(item);
    if(heal) {
        if(t->hp == 0) {
            snprintf(msg, sizeof(msg), "%s has fainted: use a Revive Crystal first", name);
            return result(false, msg);
        }
        if(t->hp >= s.max_hp) {
            snprintf(msg, sizeof(msg), "%s is already at full HP", name);
            return result(false, msg);
        }
        uint16_t before = t->hp;
        uint32_t hp = heal == ITEM_HEAL_FULL ? s.max_hp : (uint32_t)t->hp + heal;
        t->hp = hp > s.max_hp ? s.max_hp : (uint16_t)hp;
        inv->items[item]--;
        if(t->hp == s.max_hp)
            snprintf(msg, sizeof(msg), "%s is fully healed", name);
        else
            snprintf(msg, sizeof(msg), "%s +%u HP", name, t->hp - before);
    } else if(item == ItemRevive) {
        if(t->hp > 0) {
            snprintf(msg, sizeof(msg), "%s hasn't fainted", name);
            return result(false, msg);
        }
        t->hp = 1;
        inv->items[item]--;
        snprintf(msg, sizeof(msg), "%s is back with 1 HP", name);
    } else if(item == ItemCrystal) {
        if(t->level >= MAX_LEVEL) {
            snprintf(msg, sizeof(msg), "%s is at the max level", name);
            return result(false, msg);
        }
        inv->items[item]--;
        uint16_t xp = echo_xp_to_next(t->level) / 2;
        uint8_t before = t->level;
        echo_add_xp(t, xp);
        const Move* learned = NULL;
        for(uint16_t l = (uint16_t)before + 1; l <= t->level; l++) {
            const Move* m = echo_move_learned_at(t->mac, (uint8_t)l);
            if(m) learned = m;
        }
        if(learned)
            snprintf(msg, sizeof(msg), "%s reached Lv %u and learned %s", name, t->level, learned->name);
        else if(t->level > before)
            snprintf(msg, sizeof(msg), "%s +%u XP: now Lv %u", name, xp, t->level);
        else
            snprintf(msg, sizeof(msg), "%s +%u XP", name, xp);
    } else {
        return result(false, "Use balls in battle");
    }
    E.dirty = true;
    engine_save();
    return result(true, msg);
}

char* engine_set_lead(uint8_t team_index) {
    if(E.in_battle) return result(false, "Not during a battle");
    if(team_index >= E.save.team_count) return result(false, "No such team member");
    E.save.lead = team_index;
    E.dirty = true;
    engine_save();
    char name[ECHO_NAME_LEN], msg[48];
    name_of(E.save.team[team_index].mac, name, sizeof(name));
    snprintf(msg, sizeof(msg), "%s now leads your team", name);
    return result(true, msg);
}

char* engine_team_to_pac(uint8_t team_index) {
    if(E.in_battle) return result(false, "Not during a battle");
    if(team_index >= E.save.team_count) return result(false, "No such team member");
    if(E.save.team_count <= 1) return result(false, "Your team needs at least one echoid");
    if(!pac_has_room()) return result(false, "The PAC is full: craft another PAC");
    Echoid e = E.save.team[team_index];
    e.flags = 0;
    if(!pac_append(&e)) return result(false, "Couldn't write to storage");
    team_remove(E.save.team, &E.save.team_count, &E.save.lead, team_index, NULL);
    E.save.pac_count++;
    engine_save();
    char name[ECHO_NAME_LEN], msg[48];
    name_of(e.mac, name, sizeof(name));
    snprintf(msg, sizeof(msg), "%s went to the PAC", name);
    return result(true, msg);
}

char* engine_pac_to_team(uint32_t pac_index) {
    if(E.in_battle) return result(false, "Not during a battle");
    if(E.save.team_count >= PARTY_MAX) return result(false, "Your team is full: pick one to swap");
    Echoid e;
    if(pac_index >= E.save.pac_count || !pac_read(pac_index, &e)) return result(false, "Not found");
    EchoStats s;
    echo_stats(&e, &s);
    e.hp = s.max_hp; /* echoids leave the PAC fully healed */
    e.flags = 0;
    if(!pac_remove(pac_index, E.save.pac_count)) return result(false, "Couldn't write to storage");
    E.save.pac_count--;
    E.save.team[E.save.team_count++] = e;
    engine_save();
    char name[ECHO_NAME_LEN], msg[48];
    name_of(e.mac, name, sizeof(name));
    snprintf(msg, sizeof(msg), "%s joined your team", name);
    return result(true, msg);
}

char* engine_swap(uint32_t pac_index, uint8_t team_index) {
    if(E.in_battle) return result(false, "Not during a battle");
    if(team_index >= E.save.team_count) return result(false, "No such team member");
    Echoid in;
    if(pac_index >= E.save.pac_count || !pac_read(pac_index, &in)) return result(false, "Not found");
    Echoid out = E.save.team[team_index];
    EchoStats s;
    echo_stats(&in, &s);
    in.hp = s.max_hp;
    in.flags = 0;
    out.flags = 0;
    if(!pac_write(pac_index, &out)) return result(false, "Couldn't write to storage");
    E.save.team[team_index] = in;
    engine_save();
    char a[ECHO_NAME_LEN], b[ECHO_NAME_LEN], msg[64];
    name_of(in.mac, a, sizeof(a));
    name_of(out.mac, b, sizeof(b));
    snprintf(msg, sizeof(msg), "%s joined your team, %s went to the PAC", a, b);
    return result(true, msg);
}

char* engine_set_source(int demo) {
    E.save.source = demo ? ScanSourceDemo : ScanSourceBoard;
    E.q_count = 0;
    E.dirty = true;
    engine_save();
    return result(true, demo ? "Demo mode: simulated echoids" : "Using your phone's radios");
}

char* engine_set_bluetooth(bool on) {
    E.save.bt_enabled = on ? 1 : 0;
    E.dirty = true;
    engine_save();
    return result(true, on ? "Bluetooth scanning on" : "Bluetooth scanning off");
}

char* engine_reset(void) {
    if(E.in_battle) return result(false, "Finish the battle first");
    uint8_t mac[6];
    char name[NAME_LEN];
    /* The starter comes from the same device ID as before. */
    memcpy(mac, furi_hal_version_get_ble_mac(), 6);
    snprintf(name, sizeof(name), "%s", furi_hal_version_get_name_ptr());
    uint8_t source = E.save.source, bt = E.save.bt_enabled;
    new_game(mac, name);
    E.save.source = source;
    E.save.bt_enabled = bt;
    E.nearby_count = 0;
    E.spotted_count = 0;
    E.shards_this_run = 0;
    E.asleep_count = 0;
    engine_save();
    return result(true, "New game started");
}

char* engine_take_events_json(void) {
    Json j;
    json_init(&j);
    json_arr(&j);
    for(uint8_t i = 0; i < E.event_count; i++) {
        json_obj(&j);
        json_kstr(&j, "kind", E.events[i].kind);
        json_kstr(&j, "text", E.events[i].text);
        json_end_obj(&j);
    }
    json_end_arr(&j);
    E.event_count = 0;
    return json_finish(&j);
}
