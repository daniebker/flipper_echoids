/*
 * Echoids session rules, shared by the Flipper and Android apps. See session.h.
 */
#include "session.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <furi.h>
#include <furi_hal.h>

#include "pac_store.h"

#define LEARNED_MAX 8 /* moves one battle or crystal can teach, at most */

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static void say(Session* s, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void say(Session* s, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(s->msg, sizeof(s->msg), fmt, args);
    va_end(args);
}

/* Fails an action with a message. */
#define FAIL(s, ...)          \
    do {                      \
        say((s), __VA_ARGS__); \
        return false;         \
    } while(0)

static uint8_t local_hour(void) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    return dt.hour % 24;
}

static void name_of(const uint8_t mac[6], char out[ECHO_NAME_LEN]) {
    echo_name(mac, out, ECHO_NAME_LEN);
}

static void event(Session* s, uint8_t kind, const uint8_t* mac, Shard shard, const char* text) {
    if(s->event_count == SESSION_EVENTS_MAX) {
        memmove(&s->events[0], &s->events[1], sizeof(SessionEvent) * (SESSION_EVENTS_MAX - 1));
        s->event_count--;
    }
    SessionEvent* e = &s->events[s->event_count++];
    memset(e, 0, sizeof(*e));
    e->kind = kind;
    if(mac) memcpy(e->mac, mac, 6);
    e->shard = shard;
    snprintf(e->text, sizeof(e->text), "%s", text);
}

bool session_take_event(Session* s, SessionEvent* out) {
    if(s->event_count == 0) return false;
    *out = s->events[0];
    memmove(&s->events[0], &s->events[1], sizeof(SessionEvent) * (s->event_count - 1));
    s->event_count--;
    return true;
}

Echoid* session_lead(Session* s) {
    return &s->save.team[s->save.lead];
}

/* In the team or stored in the PAC (the PAC check reads storage). */
bool session_owns(Session* s, const uint8_t mac[6]) {
    for(uint8_t i = 0; i < s->save.team_count; i++)
        if(memcmp(s->save.team[i].mac, mac, 6) == 0) return true;
    return pac_find(mac, s->save.pac_count) != PAC_NOT_FOUND;
}

bool session_defeated(const Session* s, const uint8_t mac[6]) {
    uint32_t h = echo_hash_mac(mac);
    uint16_t n = s->save.defeated_count < DEFEATED_MAX ? s->save.defeated_count : DEFEATED_MAX;
    for(uint16_t i = 0; i < n; i++)
        if(s->save.defeated[i] == h) return true;
    return false;
}

/* Remembers a beaten wild echoid for good (the oldest are overwritten when full). */
static void remember_defeated(Session* s, const uint8_t mac[6]) {
    SaveData* sv = &s->save;
    if(session_defeated(s, mac)) return;
    sv->defeated[sv->defeated_next] = echo_hash_mac(mac);
    sv->defeated_next = (sv->defeated_next + 1) % DEFEATED_MAX;
    if(sv->defeated_count < DEFEATED_MAX) sv->defeated_count++;
}

/* Caught or beaten: it never shows up nearby again. */
static bool done_with(Session* s, const uint8_t mac[6]) {
    return session_defeated(s, mac) || session_owns(s, mac);
}

/* Takes an echoid off the nearby list. */
static void nearby_remove(Session* s, const uint8_t mac[6]) {
    for(uint8_t i = 0; i < s->nearby_count; i++) {
        if(memcmp(s->nearby[i].mac, mac, 6) != 0) continue;
        memmove(&s->nearby[i], &s->nearby[i + 1], sizeof(Nearby) * (s->nearby_count - i - 1));
        s->nearby_count--;
        if(s->battle_nearby > (int8_t)i)
            s->battle_nearby--;
        else if(s->battle_nearby == (int8_t)i)
            s->battle_nearby = -1;
        if(s->nearby_next_replace >= s->nearby_count) s->nearby_next_replace = 0;
        return;
    }
}

bool session_team_full(const Session* s) {
    return s->save.team_count >= PARTY_MAX;
}

bool session_pac_has_room(const Session* s) {
    return s->save.pac_count < save_pac_capacity(&s->save);
}

/* Remembers a hash in a small "seen" list; returns true if it was new. */
static bool note(uint32_t* list, uint16_t* count, uint16_t max, uint32_t h) {
    uint16_t known = *count < max ? *count : max;
    for(uint16_t i = 0; i < known; i++)
        if(list[i] == h) return false;
    if(*count < max) list[*count] = h;
    if(*count < UINT16_MAX) (*count)++;
    return true;
}

/* ------------------------------------------------------------------------- */
/* Game lifecycle                                                            */
/* ------------------------------------------------------------------------- */

/* Starts a new game, keeping the settings (board setup and Bluetooth). */
static void new_game(Session* s) {
    SaveData* sv = &s->save;
    bool keep = sv->magic == SAVE_MAGIC;
    uint8_t cfg[9] = {
        sv->uart,
        sv->bt_enabled,
        sv->baud_idx,
        sv->power,
        sv->boot_idx,
        sv->eol,
        sv->wifi_cmd,
        sv->bt_cmd,
        sv->log_sd};
    memset(sv, 0, sizeof(*sv));
    sv->magic = SAVE_MAGIC;
    sv->version = SAVE_VERSION;
    sv->uart = ScanUartUsart;
    sv->bt_enabled = 1;
    if(keep) {
        sv->uart = cfg[0];
        sv->bt_enabled = cfg[1];
        sv->baud_idx = cfg[2];
        sv->power = cfg[3];
        sv->boot_idx = cfg[4];
        sv->eol = cfg[5];
        sv->wifi_cmd = cfg[6];
        sv->bt_cmd = cfg[7];
        sv->log_sd = cfg[8];
    }
    /* The starter is born from the device's own MAC. */
    echo_init(&sv->team[0], s->device_mac, 1, s->device_name[0] ? s->device_name : "Echoids");
    sv->team_count = 1;
    sv->lead = 0;
    sv->pacs = 1; /* room for PAC_SIZE echoids to start */
    sv->pac_count = 0;
    pac_clear();
    s->dirty = true;
}

void session_save(Session* s) {
    if(save_store(&s->save)) s->dirty = false;
    s->last_save = furi_get_tick();
}

bool session_init(Session* s, const uint8_t device_mac[6], const char* device_name) {
    memset(s, 0, sizeof(*s));
    memcpy(s->device_mac, device_mac, 6);
    snprintf(s->device_name, sizeof(s->device_name), "%s", device_name ? device_name : "");
    s->battle_nearby = -1;
    game_rng_seed(furi_hal_random_get());
    bool fresh = !save_load(&s->save);
    if(fresh) {
        new_game(s);
        session_save(s);
    }
    s->hour = s->hour_checked = local_hour();
    s->last_regen = s->last_save = furi_get_tick();
    return fresh;
}

bool session_reset(Session* s) {
    if(s->in_battle) FAIL(s, "Finish the battle first");
    if(s->dungeon.active) FAIL(s, "Jack out first");
    new_game(s);
    s->nearby_count = 0;
    s->nearby_next_replace = 0;
    s->spotted_count = 0;
    s->shards_this_run = 0;
    s->asleep_count = 0;
    s->shard_log_count = 0;
    s->event_count = 0;
    session_save(s);
    say(s, "New game started");
    return true;
}

/* ------------------------------------------------------------------------- */
/* Scanning                                                                  */
/* ------------------------------------------------------------------------- */

/* Drops nearby echoids that aren't out at the current hour. */
static void nearby_prune_hour(Session* s) {
    uint8_t keep = 0;
    for(uint8_t i = 0; i < s->nearby_count; i++) {
        if(echo_active_at(s->nearby[i].mac, s->hour))
            s->nearby[keep++] = s->nearby[i];
        else
            note(s->asleep, &s->asleep_count, SESSION_ASLEEP_MAX, echo_hash_mac(s->nearby[i].mac));
    }
    s->nearby_count = keep;
    s->nearby_next_replace = 0;
}

static void handle_ap(Session* s, const ScanEvent* ev) {
    /* Echoids only come out at certain hours of the day. */
    if(!echo_active_at(ev->mac, s->hour)) {
        note(s->asleep, &s->asleep_count, SESSION_ASLEEP_MAX, echo_hash_mac(ev->mac));
        return;
    }
    for(uint8_t i = 0; i < s->nearby_count; i++) {
        if(memcmp(s->nearby[i].mac, ev->mac, 6) == 0) {
            s->nearby[i].rssi = ev->rssi;
            return;
        }
    }
    /* Caught and beaten echoids are gone for good. */
    if(done_with(s, ev->mac)) return;
    Nearby* n;
    if(s->nearby_count < SESSION_NEARBY_MAX) {
        n = &s->nearby[s->nearby_count++];
    } else {
        /* Recycle slots round-robin, but never the one in battle. */
        uint8_t slot = s->nearby_next_replace;
        if(s->in_battle && (int8_t)slot == s->battle_nearby) slot = (slot + 1) % SESSION_NEARBY_MAX;
        s->nearby_next_replace = (slot + 1) % SESSION_NEARBY_MAX;
        n = &s->nearby[slot];
    }
    memcpy(n->mac, ev->mac, 6);
    n->rssi = ev->rssi;
    snprintf(n->ssid, sizeof(n->ssid), "%s", ev->name[0] ? ev->name : "<hidden>");
    n->state = NearbyWild;

    /* Count each echoid once, even if its list slot was recycled. */
    if(note(s->spotted, &s->spotted_count, SESSION_SPOTTED_MAX, echo_hash_mac(ev->mac))) {
        char name[ECHO_NAME_LEN], text[SESSION_MSG_LEN];
        name_of(ev->mac, name);
        snprintf(text, sizeof(text), "A wild %s is out!", name);
        event(s, SessionEventSpotted, ev->mac, (Shard){0, 0}, text);
    }
}

/* Puts a device at the top of the shard log (once per device). */
static void shard_log(Session* s, const ScanEvent* ev, Shard sh, bool collected, uint32_t ready_at) {
    uint8_t n = s->shard_log_count;
    bool was_collected = false;
    for(uint8_t i = 0; i < n; i++) {
        if(memcmp(s->shard_log[i].mac, ev->mac, 6) == 0) {
            was_collected = s->shard_log[i].collected;
            memmove(&s->shard_log[i], &s->shard_log[i + 1], sizeof(ShardSeen) * (n - i - 1));
            n--;
            break;
        }
    }
    if(n == SESSION_SHARDS_MAX) n--; /* the oldest drops off */
    memmove(&s->shard_log[1], &s->shard_log[0], sizeof(ShardSeen) * n);
    ShardSeen* e = &s->shard_log[0];
    memset(e, 0, sizeof(*e));
    memcpy(e->mac, ev->mac, 6);
    e->shard = sh;
    e->collected = collected || was_collected;
    e->ready_at = ready_at;
    snprintf(e->name, sizeof(e->name), "%s", ev->name);
    s->shard_log_count = n + 1;
}

uint32_t session_shard_left(const Session* s, uint8_t i) {
    if(i >= s->shard_log_count) return 0;
    uint32_t now = furi_hal_rtc_get_timestamp();
    uint32_t at = s->shard_log[i].ready_at;
    return at > now ? at - now : 0;
}

static void handle_bt(Session* s, const ScanEvent* ev) {
    SaveData* sv = &s->save;
    uint32_t h = echo_hash_mac(ev->mac);
    uint32_t remaining = 0;
    Shard sh = shard_from_mac(ev->mac);
    char text[SESSION_MSG_LEN];
    /* Each device recharges before it drops its shard again; rarer shards take longer. */
    if(!shard_claim(
           sv->seen_bt,
           sv->seen_bt_time,
           &sv->seen_bt_count,
           SEEN_BT_MAX,
           h,
           sh.tier,
           furi_hal_rtc_get_timestamp(),
           &remaining)) {
        /* Still recharging: it goes in the shard log with the time left. */
        shard_log(s, ev, sh, false, furi_hal_rtc_get_timestamp() + remaining);
        return;
    }
    shard_log(s, ev, sh, true, furi_hal_rtc_get_timestamp() + shard_cooldown_s(sh.tier));
    if(sv->inv.shards[sh.type][sh.tier] < 9999) sv->inv.shards[sh.type][sh.tier]++;
    sv->shards_found++;
    if(s->shards_this_run < UINT16_MAX) s->shards_this_run++;
    s->dirty = true;
    snprintf(text, sizeof(text), "+1 %s %s shard", shard_tier_name(sh.tier), echo_type_name(sh.type));
    event(s, SessionEventShard, ev->mac, sh, text);
}

static void handle(Session* s, const ScanEvent* ev) {
    if(ev->type == ScanEventAp)
        handle_ap(s, ev);
    else if(s->save.bt_enabled)
        handle_bt(s, ev);
}

void session_sighting(Session* s, const ScanEvent* ev) {
    if(s->scanning) handle(s, ev);
}

void session_scan_start(Session* s) {
    if(s->dungeon.active) return; /* no collecting inside a dungeon */
    /* Catches since the last visit may have been seen here before. */
    for(uint8_t i = s->nearby_count; i-- > 0;)
        if(done_with(s, s->nearby[i].mac)) nearby_remove(s, s->nearby[i].mac);
    s->scanning = true;
}

void session_scan_stop(Session* s) {
    s->scanning = false;
    if(s->dirty) session_save(s);
}

void session_set_bluetooth(Session* s, bool on) {
    s->save.bt_enabled = on ? 1 : 0;
    session_save(s);
    say(s, on ? "Bluetooth shards on" : "Bluetooth shards off");
}

/* ------------------------------------------------------------------------- */
/* Tick                                                                      */
/* ------------------------------------------------------------------------- */

void session_tick(Session* s) {
    uint32_t now = furi_get_tick();

    /* A new hour brings different echoids (after any battle). */
    s->hour = local_hour();
    if(s->hour != s->hour_checked && !s->in_battle) {
        s->hour_checked = s->hour;
        s->asleep_count = 0;
        nearby_prune_hour(s);
    }

    if(now - s->last_regen >= furi_ms_to_ticks(SESSION_REGEN_MS)) {
        s->last_regen = now;
        if(!s->in_battle) {
            for(uint8_t i = 0; i < s->save.team_count; i++) {
                Echoid* e = &s->save.team[i];
                EchoStats st;
                echo_stats(e, &st);
                /* Fainted echoids stay down until a Revive Crystal. */
                if(e->hp > 0 && e->hp < st.max_hp) e->hp++;
            }
        }
    }

    if(s->dirty && !s->in_battle && now - s->last_save >= furi_ms_to_ticks(SESSION_AUTOSAVE_MS))
        session_save(s);
}

/* ------------------------------------------------------------------------- */
/* Queries                                                                   */
/* ------------------------------------------------------------------------- */

void session_nearby_echoid(Session* s, uint8_t index, Echoid* out) {
    const Nearby* n = &s->nearby[index % SESSION_NEARBY_MAX];
    /* Wild levels follow your lead. */
    echo_init(out, n->mac, echo_wild_level(n->mac, session_lead(s)->level), n->ssid);
}

const char* session_catch_blocked(Session* s) {
    if(s->battle_in_dungeon) return "Can't catch in a dungeon";
    if(session_owns(s, s->battle.wild.mac)) return "Already caught this one!";
    if(session_team_full(s) && !session_pac_has_room(s)) return "No room: craft a PAC!";
    return NULL;
}

ItemUse session_item_use(uint8_t item) {
    if(item_heal_amount(item)) return ItemUseHeal;
    if(item == ItemRevive) return ItemUseRevive;
    if(item == ItemCrystal) return ItemUseXp;
    return ItemUseNone;
}

uint32_t session_recipe_have(const Session* s, size_t recipe) {
    if(recipe >= recipe_count) return 0;
    uint8_t out = recipes[recipe].out;
    if(out == CRAFT_OUT_PAC) return s->save.pacs;
    return out < ItemCount ? s->save.inv.items[out] : 0;
}

/* ------------------------------------------------------------------------- */
/* Battle                                                                    */
/* ------------------------------------------------------------------------- */

static void battle_log(Session* s, const char* msg) {
    Battle* b = &s->battle;
    memmove(b->log[0], b->log[1], BATTLE_LOG_LEN);
    snprintf(b->log[1], BATTLE_LOG_LEN, "%s", msg);
}

/* Adds a results line (cut to fit the screen). */
static void result_add(Session* s, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void result_add(Session* s, const char* fmt, ...) {
    if(s->result_count >= SESSION_RESULTS_MAX) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(s->results[s->result_count++], SESSION_RESULT_LEN, fmt, args);
    va_end(args);
}

static Nearby* battle_nearby(Session* s) {
    if(s->battle_nearby < 0 || s->battle_nearby >= s->nearby_count) return NULL;
    Nearby* n = &s->nearby[s->battle_nearby];
    return memcmp(n->mac, s->battle.wild.mac, 6) == 0 ? n : NULL;
}

bool session_battle_others_can_fight(Session* s) {
    for(uint8_t i = 0; i < s->save.team_count; i++)
        if(&s->save.team[i] != s->battle.mine && s->save.team[i].hp > 0) return true;
    return false;
}

/* What everyone who fought got out of the battle. */
static void build_results(Session* s) {
    Battle* b = &s->battle;
    s->result_count = 0;
    for(uint8_t i = 0; i < b->fought_n; i++) {
        Echoid* e = b->fought[i];
        uint8_t before = b->fought_level_before[i];
        char name[ECHO_NAME_LEN];
        name_of(e->mac, name);
        if(e->level > before)
            result_add(s, "%s +%lu Lv%u>%u", name, (unsigned long)b->fought_xp[i], before, e->level);
        else
            result_add(s, "%s +%lu XP", name, (unsigned long)b->fought_xp[i]);
        const Move* learned[LEARNED_MAX];
        uint8_t n = echo_moves_learned(e->mac, before, e->level, learned, LEARNED_MAX);
        for(uint8_t k = 0; k < n; k++)
            result_add(s, "  learned %s", learned[k]->name);
    }
}

/* Leaves the dungeon (rewards already given). */
static void dungeon_end(Session* s) {
    s->dungeon.active = false;
    s->dirty = true;
}

/* Thrown out of the dungeon, with a reason for the player. */
static void dungeon_eject(Session* s, const char* why) {
    dungeon_end(s);
    say(s, "%s", why);
    event(s, SessionEventEjected, s->dungeon.mac, (Shard){0, 0}, why);
}

/* The boss is beaten: rewards, listed with the battle results. */
static void dungeon_clear(Session* s) {
    Dungeon* d = &s->dungeon;
    Inventory* inv = &s->save.inv;
    d->cleared = true;
    if(s->save.dungeons_cleared < UINT16_MAX) s->save.dungeons_cleared++;
    if(inv->shards[d->map.type][ShardPrismatic] < 9999) inv->shards[d->map.type][ShardPrismatic]++;
    if(inv->items[ItemGreatBall] < 9997) inv->items[ItemGreatBall] += 2;
    if(inv->items[ItemRevive] < 9999) inv->items[ItemRevive]++;
    result_add(s, "Dungeon cleared! Found:");
    result_add(s, "  1 Prismatic %s shard", echo_type_name(d->map.type));
    result_add(s, "  2 Great Balls, 1 Revive");
}

/* After every battle action: forced switches, captures, wins and XP. */
static void after_action(Session* s) {
    Battle* b = &s->battle;
    /* Fainted with healthy echoids left: the battle goes on with a replacement. */
    if(b->result == BattleLost && session_battle_others_can_fight(s)) {
        b->result = BattleOngoing;
        s->must_switch = true;
        return;
    }
    if(b->result == BattleOngoing) return;
    Nearby* n = battle_nearby(s);
    char msg[BATTLE_LOG_LEN];

    switch(b->result) {
    case BattleCaptured: {
        SaveData* sv = &s->save;
        Echoid caught = b->wild;
        if(caught.hp == 0) caught.hp = 1;
        caught.flags = 0;
        bool stored = false;
        if(session_owns(s, caught.mac)) {
            /* Never store a second copy of an echoid you already have. */
            battle_log(s, "Already caught this one!");
        } else if(sv->team_count < PARTY_MAX) {
            sv->team[sv->team_count++] = caught;
            stored = true;
        } else if(session_pac_has_room(s) && pac_append(&caught)) {
            sv->pac_count++;
            battle_log(s, "Team full: sent to PAC");
            stored = true;
        } else {
            battle_log(s, "Couldn't store it!");
        }
        if(stored) sv->captures++;
        if(n && (stored || session_owns(s, caught.mac))) n->state = NearbyOwned;
        break;
    }
    case BattleWon:
        s->save.wins++;
        /* A beaten wild echoid is gone for good (dungeon foes aren't nearby echoids). */
        if(s->battle_in_dungeon)
            s->dungeon.room_done = true;
        else
            remember_defeated(s, b->wild.mac);
        if(n) n->state = NearbyDone;
        break;
    case BattleFled:
        /* It got away: it stays in the list and can be battled again. */
        break;
    default:
        break;
    }

    if(b->xp_gained) {
        if(b->fought_n > 1)
            snprintf(msg, sizeof(msg), "+%lu XP shared by %u", (unsigned long)b->xp_gained, b->fought_n);
        else
            snprintf(msg, sizeof(msg), "+%lu XP", (unsigned long)b->xp_gained);
        battle_log(s, msg);
        /* A one-line summary; the results have the details. */
        uint8_t levelled = 0;
        const Move* learned = NULL;
        for(uint8_t i = 0; i < b->fought_n; i++) {
            Echoid* e = b->fought[i];
            if(e->level > b->fought_level_before[i]) levelled++;
            const Move* m[LEARNED_MAX];
            uint8_t k = echo_moves_learned(e->mac, b->fought_level_before[i], e->level, m, LEARNED_MAX);
            if(k) learned = m[k - 1];
        }
        if(learned) {
            snprintf(msg, sizeof(msg), "Learned %s!", learned->name);
            battle_log(s, msg);
        } else if(levelled) {
            snprintf(msg, sizeof(msg), "%u level up%s!", levelled, levelled > 1 ? "s" : "");
            battle_log(s, msg);
        }
    }
    build_results(s);
    if(s->battle_in_dungeon && b->result == BattleWon &&
       s->dungeon.map.kind[s->dungeon.room] == RoomBoss)
        dungeon_clear(s);
    s->dirty = true;
}

bool session_battle_begin(Session* s, uint8_t index) {
    if(s->in_battle) FAIL(s, "Already in a battle");
    if(s->dungeon.active) FAIL(s, "Jack out first");
    if(index >= s->nearby_count) FAIL(s, "It's gone");
    Nearby* n = &s->nearby[index];
    /* Check ownership live (team and PAC), not just the list's mark. */
    if(n->state == NearbyOwned || session_owns(s, n->mac)) {
        n->state = NearbyOwned;
        FAIL(s, "Already caught");
    }
    if(n->state == NearbyDone || session_defeated(s, n->mac)) {
        n->state = NearbyDone;
        FAIL(s, "Already beaten - find others");
    }
    if(!echo_active_at(n->mac, s->hour)) FAIL(s, "It went to sleep...");
    /* The lead fights; if it has fainted, the first healthy team member does. */
    Echoid* first = session_lead(s);
    if(first->hp == 0) {
        first = NULL;
        for(uint8_t i = 0; i < s->save.team_count && !first; i++)
            if(s->save.team[i].hp > 0) first = &s->save.team[i];
    }
    if(!first) FAIL(s, "All fainted: use Revives");
    /* Wild levels follow your lead, even if another echoid starts. */
    battle_start(&s->battle, first, n->mac, echo_wild_level(n->mac, session_lead(s)->level), n->ssid);
    s->battle_nearby = (int8_t)index;
    s->battle_in_dungeon = false;
    s->in_battle = true;
    s->must_switch = false;
    s->result_count = 0;
    s->msg[0] = '\0';
    return true;
}

static bool can_act(Session* s) {
    if(!s->in_battle || s->battle.result != BattleOngoing) FAIL(s, "The battle is over");
    if(s->must_switch) FAIL(s, "Send in another echoid");
    return true;
}

bool session_battle_move(Session* s, uint8_t slot) {
    if(!can_act(s)) return false;
    battle_attack(&s->battle, slot);
    after_action(s);
    return true;
}

bool session_battle_can_switch_to(Session* s, uint8_t team_index) {
    if(!s->in_battle || s->battle.result != BattleOngoing) FAIL(s, "The battle is over");
    if(team_index >= s->save.team_count) FAIL(s, "No such team member");
    Echoid* next = &s->save.team[team_index];
    if(next == s->battle.mine) FAIL(s, "Already fighting");
    if(next->hp == 0) FAIL(s, "It has fainted");
    return true;
}

bool session_battle_switch(Session* s, uint8_t team_index) {
    if(!session_battle_can_switch_to(s, team_index)) return false;
    /* A chosen switch costs the turn; replacing a fainted echoid doesn't. */
    battle_switch(&s->battle, &s->save.team[team_index], !s->must_switch);
    s->must_switch = false;
    after_action(s);
    return true;
}

bool session_battle_ball(Session* s, uint8_t ball) {
    if(!can_act(s)) return false;
    const char* why = session_catch_blocked(s);
    if(why) FAIL(s, "%s", why);
    if(!battle_throw(&s->battle, &s->save.inv, ball)) {
        battle_log(s, "No balls of that kind!");
        FAIL(s, "You have none of those");
    }
    s->dirty = true;
    after_action(s);
    return true;
}

bool session_battle_run(Session* s) {
    if(!can_act(s)) return false;
    if(s->battle_in_dungeon) FAIL(s, "No running in a dungeon!");
    battle_flee(&s->battle);
    after_action(s);
    return true;
}

bool session_battle_close(Session* s) {
    if(!s->in_battle) return true;
    if(s->battle.result == BattleOngoing) FAIL(s, "The battle isn't over");
    s->in_battle = false;
    s->must_switch = false;
    /* Caught or beaten: off the list now that the battle screen is closed. */
    Nearby* n = battle_nearby(s);
    if(n && n->state != NearbyWild) nearby_remove(s, n->mac);
    s->battle_nearby = -1;
    if(s->battle_in_dungeon) {
        s->battle_in_dungeon = false;
        if(s->battle.result == BattleLost)
            dungeon_eject(s, "Team fainted: ejected!");
        else if(s->dungeon.link_lost)
            dungeon_eject(s, "Connection lost: ejected!");
    }
    session_save(s);
    return true;
}

/* ------------------------------------------------------------------------- */
/* Dungeons                                                                  */
/* ------------------------------------------------------------------------- */

bool session_in_dungeon(const Session* s) {
    return s->dungeon.active;
}

static bool team_can_fight(const Session* s) {
    for(uint8_t i = 0; i < s->save.team_count; i++)
        if(s->save.team[i].hp > 0) return true;
    return false;
}

bool session_jack_in_check(Session* s, uint8_t index) {
    if(s->in_battle) FAIL(s, "Finish the battle first");
    if(s->dungeon.active) FAIL(s, "Already jacked in");
    if(index >= s->nearby_count) FAIL(s, "It's gone");
    if(!team_can_fight(s)) FAIL(s, "All fainted: use Revives");
    return true;
}

void session_link_up(Session* s, const uint8_t mac[6]) {
    s->link_up = true;
    memcpy(s->link_mac, mac, 6);
}

void session_link_down(Session* s) {
    s->link_up = false;
    if(!s->dungeon.active) return;
    /* Out now, or as soon as the fight is over. */
    if(s->in_battle && s->battle_in_dungeon)
        s->dungeon.link_lost = true;
    else
        dungeon_eject(s, "Connection lost: ejected!");
}

bool session_wants_link(const Session* s) {
    return s->dungeon.active;
}

bool session_jack_in(Session* s, uint8_t index) {
    if(!session_jack_in_check(s, index)) return false;
    const Nearby* n = &s->nearby[index];
    /* You have to be connected to the network itself. */
    if(!s->link_up || memcmp(s->link_mac, n->mac, 6) != 0) FAIL(s, "Connect to the network first");
    Dungeon* d = &s->dungeon;
    memset(d, 0, sizeof(*d));
    memcpy(d->mac, n->mac, 6);
    snprintf(d->ssid, sizeof(d->ssid), "%s", n->ssid);
    dungeon_name(n->mac, d->name, sizeof(d->name));
    dungeon_layout(n->mac, &d->map);
    d->active = true;
    if(s->scanning) session_scan_stop(s);
    say(s, "Jacked in: %s", d->name);
    return true;
}

uint8_t session_team_level(const Session* s) {
    uint32_t sum = 0;
    for(uint8_t i = 0; i < s->save.team_count; i++)
        sum += s->save.team[i].level;
    uint8_t n = s->save.team_count ? s->save.team_count : 1;
    return (uint8_t)((sum + n - 1) / n);
}

bool session_dungeon_foe(Session* s, uint8_t room, Echoid* out) {
    const Dungeon* d = &s->dungeon;
    if(!d->active || room >= d->map.rooms) return false;
    uint8_t kind = d->map.kind[room];
    if(kind != RoomFight && kind != RoomBoss) return false;
    echo_init(out, d->map.foe[room], dungeon_foe_level(session_team_level(s), kind == RoomBoss), d->ssid);
    return true;
}

bool session_dungeon_room(Session* s) {
    Dungeon* d = &s->dungeon;
    if(!d->active) FAIL(s, "Not in a dungeon");
    if(s->in_battle) FAIL(s, "Finish the battle first");
    if(d->room_done) FAIL(s, d->cleared ? "Dungeon cleared!" : "Room cleared: go deeper");
    switch(d->map.kind[d->room]) {
    case RoomCache: {
        uint8_t count, tier;
        dungeon_cache(d->mac, d->room, &count, &tier);
        uint16_t* have = &s->save.inv.shards[d->map.type][tier];
        *have = *have + count > 9999 ? 9999 : (uint16_t)(*have + count);
        d->room_done = true;
        say(s, "Found %u %s %s shards", count, shard_tier_name(tier), echo_type_name(d->map.type));
        session_save(s);
        return true;
    }
    case RoomRest:
        for(uint8_t i = 0; i < s->save.team_count; i++) {
            Echoid* e = &s->save.team[i];
            EchoStats st;
            echo_stats(e, &st);
            if(e->hp == 0) continue; /* fainted ones need a Revive Crystal */
            uint32_t hp = e->hp + (uint32_t)st.max_hp * DUNGEON_REST_PCT / 100;
            e->hp = hp > st.max_hp ? st.max_hp : (uint16_t)hp;
        }
        d->room_done = true;
        say(s, "The team rests and recovers");
        session_save(s);
        return true;
    default: {
        /* A fight or the boss: the first healthy team member, lead first. */
        Echoid* first = session_lead(s);
        if(first->hp == 0) {
            first = NULL;
            for(uint8_t i = 0; i < s->save.team_count && !first; i++)
                if(s->save.team[i].hp > 0) first = &s->save.team[i];
        }
        if(!first) FAIL(s, "All fainted: use Revives");
        Echoid foe;
        session_dungeon_foe(s, d->room, &foe);
        battle_start(&s->battle, first, foe.mac, foe.level, d->ssid);
        s->battle_nearby = -1;
        s->battle_in_dungeon = true;
        s->in_battle = true;
        s->must_switch = false;
        s->result_count = 0;
        s->msg[0] = '\0';
        return true;
    }
    }
}

bool session_dungeon_next(Session* s) {
    Dungeon* d = &s->dungeon;
    if(!d->active) FAIL(s, "Not in a dungeon");
    if(!d->room_done) FAIL(s, "Clear this room first");
    if(d->room + 1 >= d->map.rooms) FAIL(s, "Dungeon cleared!");
    d->room++;
    d->room_done = false;
    s->msg[0] = '\0';
    return true;
}

bool session_dungeon_leave(Session* s) {
    if(!s->dungeon.active) FAIL(s, "Not in a dungeon");
    if(s->in_battle) FAIL(s, "Finish the battle first");
    bool cleared = s->dungeon.cleared;
    dungeon_end(s);
    session_save(s);
    say(s, cleared ? "Jacked out. Well done!" : "Jacked out");
    return true;
}

/* ------------------------------------------------------------------------- */
/* Items, crafting, team and PAC                                             */
/* ------------------------------------------------------------------------- */

/* Things you can't do inside a dungeon: only the team and bag are there. */
static bool not_in_dungeon(Session* s) {
    if(s->dungeon.active) FAIL(s, "Not in a dungeon");
    return true;
}

bool session_craft(Session* s, size_t recipe) {
    if(!not_in_dungeon(s)) return false;
    if(recipe >= recipe_count) FAIL(s, "No such recipe");
    if(!craft_do(&s->save.inv, recipe)) FAIL(s, "Not enough shards");
    if(recipes[recipe].out == CRAFT_OUT_PAC) {
        if(s->save.pacs < UINT32_MAX / PAC_SIZE) s->save.pacs++;
        say(s, "New PAC! Room for %lu", (unsigned long)save_pac_capacity(&s->save));
    } else {
        say(s, "Crafted %s!", recipes[recipe].name);
    }
    session_save(s);
    return true;
}

static bool not_in_battle(Session* s) {
    if(s->in_battle) FAIL(s, "Not during a battle");
    return true;
}

bool session_use_item(Session* s, uint8_t item, uint8_t team_index) {
    if(!not_in_battle(s)) return false;
    if(item >= ItemCount || team_index >= s->save.team_count) FAIL(s, "Can't use that");
    Inventory* inv = &s->save.inv;
    Echoid* t = &s->save.team[team_index];
    EchoStats st;
    echo_stats(t, &st);
    char name[ECHO_NAME_LEN];
    name_of(t->mac, name);
    if(session_item_use(item) == ItemUseNone) FAIL(s, "Use balls in battle");
    if(inv->items[item] == 0) FAIL(s, "You have none");

    switch(session_item_use(item)) {
    case ItemUseHeal: {
        /* A fainted echoid needs a Revive Crystal first. */
        if(t->hp == 0) FAIL(s, "%s fainted: revive first", name);
        if(t->hp >= st.max_hp) FAIL(s, "%s is at full HP", name);
        uint16_t heal = item_heal_amount(item);
        uint16_t before = t->hp;
        uint32_t hp = heal == ITEM_HEAL_FULL ? st.max_hp : (uint32_t)t->hp + heal;
        t->hp = hp > st.max_hp ? st.max_hp : (uint16_t)hp;
        inv->items[item]--;
        if(t->hp == st.max_hp)
            say(s, "%s fully healed!", name);
        else
            say(s, "%s +%u HP", name, t->hp - before);
        break;
    }
    case ItemUseRevive:
        if(t->hp > 0) FAIL(s, "%s hasn't fainted", name);
        t->hp = 1;
        inv->items[item]--;
        say(s, "%s revived (1 HP)", name);
        break;
    default: {
        if(t->level >= MAX_LEVEL) FAIL(s, "%s is max level", name);
        inv->items[item]--;
        uint16_t xp = echo_xp_to_next(t->level) / 2;
        uint8_t before = t->level;
        echo_add_xp(t, xp);
        const Move* learned[LEARNED_MAX];
        uint8_t n = echo_moves_learned(t->mac, before, t->level, learned, LEARNED_MAX);
        if(n)
            say(s, "Lv%u! Learned %s", t->level, learned[n - 1]->name);
        else if(t->level > before)
            say(s, "%s +%u XP! Lv%u", name, xp, t->level);
        else
            say(s, "%s +%u XP", name, xp);
        break;
    }
    }
    session_save(s);
    return true;
}

bool session_set_lead(Session* s, uint8_t team_index) {
    if(!not_in_battle(s)) return false;
    if(team_index >= s->save.team_count) FAIL(s, "No such team member");
    char name[ECHO_NAME_LEN];
    name_of(s->save.team[team_index].mac, name);
    if(s->save.lead == team_index) FAIL(s, "%s already leads", name);
    s->save.lead = team_index;
    session_save(s);
    say(s, "%s is now lead", name);
    return true;
}

bool session_team_to_pac(Session* s, uint8_t team_index) {
    if(!not_in_battle(s)) return false;
    if(!not_in_dungeon(s)) return false;
    if(team_index >= s->save.team_count) FAIL(s, "No such team member");
    if(s->save.team_count <= 1) FAIL(s, "Team needs one echoid");
    if(!session_pac_has_room(s)) FAIL(s, "PAC full: craft a PAC");
    Echoid e = s->save.team[team_index];
    e.flags = 0;
    if(!pac_append(&e)) FAIL(s, "Storage write failed");
    team_remove(s->save.team, &s->save.team_count, &s->save.lead, team_index, NULL);
    s->save.pac_count++;
    session_save(s); /* keep save.bin in step with pac.bin */
    char name[ECHO_NAME_LEN];
    name_of(e.mac, name);
    say(s, "%s sent to PAC", name);
    return true;
}

/* Echoids leave the PAC fully healed. */
static void leave_pac(Echoid* e) {
    EchoStats st;
    echo_stats(e, &st);
    e->hp = st.max_hp;
    e->flags = 0;
}

bool session_pac_to_team(Session* s, uint32_t pac_index) {
    if(!not_in_battle(s)) return false;
    if(!not_in_dungeon(s)) return false;
    if(session_team_full(s)) FAIL(s, "Team full: pick a swap");
    Echoid e;
    if(pac_index >= s->save.pac_count || !pac_read(pac_index, &e)) FAIL(s, "Not found");
    leave_pac(&e);
    if(!pac_remove(pac_index, s->save.pac_count)) FAIL(s, "Storage write failed");
    s->save.pac_count--;
    s->save.team[s->save.team_count++] = e;
    session_save(s);
    char name[ECHO_NAME_LEN];
    name_of(e.mac, name);
    say(s, "%s joined the team", name);
    return true;
}

bool session_swap(Session* s, uint32_t pac_index, uint8_t team_index) {
    if(!not_in_battle(s)) return false;
    if(!not_in_dungeon(s)) return false;
    if(team_index >= s->save.team_count) FAIL(s, "No such team member");
    Echoid in;
    if(pac_index >= s->save.pac_count || !pac_read(pac_index, &in)) FAIL(s, "Not found");
    Echoid out = s->save.team[team_index];
    leave_pac(&in);
    out.flags = 0;
    if(!pac_write(pac_index, &out)) FAIL(s, "Storage write failed");
    s->save.team[team_index] = in;
    session_save(s);
    char a[ECHO_NAME_LEN], b[ECHO_NAME_LEN];
    name_of(in.mac, a);
    name_of(out.mac, b);
    say(s, "%s <> %s", a, b);
    return true;
}
