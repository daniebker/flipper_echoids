/*
 * Tests the Android app's game engine on a PC, on the same storage shim the
 * APK uses: scanning, shards, crafting, battles and catching, items, team and
 * PAC moves, and saving/reloading. See .github/workflows/android.yml.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "engine.h"
#include "shim.h"

static int failures;
#define CHECK(c)                                                    \
    do {                                                            \
        if(!(c)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);     \
            failures++;                                             \
        }                                                           \
    } while(0)

/* A strict JSON syntax checker, so the app's org.json parser never sees bad output. */
static const char* jv(const char* p);
static const char* ws(const char* p) {
    while(*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') p++;
    return p;
}
static const char* jstr(const char* p) {
    if(*p++ != '"') return NULL;
    while(*p && *p != '"') {
        if((unsigned char)*p < 0x20) return NULL;
        if(*p == '\\') {
            p++;
            if(*p == 'u') {
                for(int i = 1; i <= 4; i++)
                    if(!strchr("0123456789abcdefABCDEF", p[i]) || !p[i]) return NULL;
                p += 4;
            } else if(!*p || !strchr("\"\\/bfnrt", *p)) return NULL;
        }
        p++;
    }
    return *p == '"' ? p + 1 : NULL;
}
static const char* jv(const char* p) {
    p = ws(p);
    if(*p == '{' || *p == '[') {
        char close = *p == '{' ? '}' : ']';
        int obj = *p == '{';
        p = ws(p + 1);
        if(*p == close) return p + 1;
        for(;;) {
            if(obj) {
                p = jstr(ws(p));
                if(!p) return NULL;
                p = ws(p);
                if(*p++ != ':') return NULL;
            }
            p = jv(p);
            if(!p) return NULL;
            p = ws(p);
            if(*p == ',') { p++; continue; }
            return *p == close ? p + 1 : NULL;
        }
    }
    if(*p == '"') return jstr(p);
    if(!strncmp(p, "true", 4)) return p + 4;
    if(!strncmp(p, "false", 5)) return p + 5;
    if(!strncmp(p, "null", 4)) return p + 4;
    const char* s = p;
    if(*p == '-') p++;
    if(!(*p >= '0' && *p <= '9')) return NULL;
    while(*p >= '0' && *p <= '9') p++;
    (void)s;
    return p;
}
static int valid(const char* json) {
    if(!json) return 0;
    const char* end = jv(json);
    return end && *ws(end) == '\0';
}
static int bad_json;
/* Every engine call in this test goes through here. */
static char* chk(char* json) {
    if(!valid(json)) {
        if(bad_json++ < 3) printf("BAD JSON: %.200s\n", json ? json : "(null)");
    }
    return json;
}
#define engine_state_json() chk(engine_state_json())
#define engine_nearby_json() chk(engine_nearby_json())
#define engine_team_json() chk(engine_team_json())
#define engine_pac_json(a, b) chk(engine_pac_json(a, b))
#define engine_echoid_json(a, b) chk(engine_echoid_json(a, b))
#define engine_inventory_json() chk(engine_inventory_json())
#define engine_battle_json() chk(engine_battle_json())
#define engine_take_events_json() chk(engine_take_events_json())
#define engine_battle_begin(a) chk(engine_battle_begin(a))
#define engine_battle_move(a) chk(engine_battle_move(a))
#define engine_battle_switch(a) chk(engine_battle_switch(a))
#define engine_battle_ball(a) chk(engine_battle_ball(a))
#define engine_battle_run() chk(engine_battle_run())
#define engine_battle_close() chk(engine_battle_close())
#define engine_craft(a) chk(engine_craft(a))
#define engine_use_item(a, b) chk(engine_use_item(a, b))
#define engine_set_lead(a) chk(engine_set_lead(a))
#define engine_team_to_pac(a) chk(engine_team_to_pac(a))
#define engine_pac_to_team(a) chk(engine_pac_to_team(a))
#define engine_swap(a, b) chk(engine_swap(a, b))
#define engine_set_bluetooth(a) chk(engine_set_bluetooth(a))
#define engine_reset() chk(engine_reset())
#define engine_dungeon_json() chk(engine_dungeon_json())
#define engine_shards_json() chk(engine_shards_json())
#define engine_jack_in_check(a) chk(engine_jack_in_check(a))
#define engine_jack_in(a) chk(engine_jack_in(a))
#define engine_dungeon_room() chk(engine_dungeon_room())
#define engine_dungeon_next() chk(engine_dungeon_next())
#define engine_dungeon_leave() chk(engine_dungeon_leave())

/* Calls an action and reports whether it succeeded. */
static int ok(char* json) {
    int r = json && strstr(json, "\"ok\":true") != NULL;
    free(json);
    return r;
}

static int contains(char* json, const char* what) {
    int r = json && strstr(json, what) != NULL;
    free(json);
    return r;
}

static long field(const char* json, const char* key) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char* p = strstr(json, pat);
    return p ? strtol(p + strlen(pat), NULL, 10) : -1;
}

static long state(const char* key) {
    char* s = engine_state_json();
    long v = field(s, key);
    free(s);
    return v;
}

static void sighting(int type, int i) {
    ScanEvent e;
    memset(&e, 0, sizeof(e));
    e.type = type ? ScanEventBt : ScanEventAp;
    uint8_t mac[6] = {(uint8_t)(type ? 0xB0 : 0xA0), 0x20, 0x30, 0x40, (uint8_t)(i >> 8), (uint8_t)i};
    memcpy(e.mac, mac, 6);
    e.rssi = (int8_t)(-50 - i % 40);
    snprintf(e.name, sizeof(e.name), "Net \"%d\"", i); /* quotes must be escaped in JSON */
    engine_push_sighting(&e);
}

/* Heals everyone in the team: revive, then a Max Tonic. */
static void heal_team(void) {
    for(uint8_t k = 0; k < 6; k++) {
        free(engine_use_item(ItemRevive, k));
        free(engine_use_item(ItemMaxTonic, k));
    }
}

/* Fights the current battle to the end with the first move. */
static void fight(void) {
    for(int t = 0; t < 400; t++) {
        char* b = engine_battle_json();
        int over = strstr(b, "\"result\":\"ongoing\"") == NULL;
        int must = strstr(b, "\"mustSwitch\":true") != NULL;
        free(b);
        if(over) return;
        if(must) {
            for(uint8_t k = 0; k < 6; k++)
                if(ok(engine_battle_switch(k))) break;
        } else {
            free(engine_battle_move(0));
        }
    }
}

static void test_dungeon(void) {
    char* list = engine_nearby_json();
    CHECK(strstr(list, "\"index\":0") != NULL);
    char mac_hex[18] = {0};
    const char* m = strstr(list, "\"mac\":\"");
    if(m) memcpy(mac_hex, m + 7, 17);
    free(list);
    uint8_t mac[6];
    for(int i = 0; i < 6; i++)
        mac[i] = (uint8_t)strtol(mac_hex + i * 3, NULL, 16);

    /* Stock up first: crafting is closed inside a dungeon. More devices give
     * enough shards for Max Tonics and Revive Crystals. */
    engine_scan_set(true);
    for(int i = 3000; i < 5000; i++) {
        sighting(1, i);
        if(i % 100 == 99) engine_tick();
    }
    engine_tick();
    free(engine_take_events_json());
    for(int i = 0; i < 30; i++) {
        free(engine_craft(6)); /* Max Tonic */
        free(engine_craft(7)); /* Revive Crystal */
    }
    heal_team();

    /* You must be connected to that network to jack in. */
    CHECK(ok(engine_jack_in_check(0)));
    CHECK(!ok(engine_jack_in(0)));
    uint8_t other[6] = {1, 2, 3, 4, 5, 6};
    engine_link_up(other);
    CHECK(!ok(engine_jack_in(0)));
    engine_link_up(mac);
    CHECK(ok(engine_jack_in(0)));
    CHECK(contains(engine_state_json(), "\"inDungeon\":true"));
    CHECK(contains(engine_state_json(), "\"wantsLink\":true"));
    CHECK(contains(engine_dungeon_json(), "\"kind\":\"boss\""));

    /* Only the team and bag: no collecting, catching, crafting, PAC or running. */
    CHECK(!ok(engine_battle_begin(0)));
    CHECK(!ok(engine_craft(0)));
    CHECK(!ok(engine_pac_to_team(0)));
    CHECK(!ok(engine_reset()));
    CHECK(!ok(engine_dungeon_next())); /* the first room isn't cleared */

    /* Play through to the boss, which is above the team's level: if it wins,
     * heal up and try again (the dungeon starts over). */
    long rooms = 0;
    for(int attempt = 0; attempt < 4; attempt++) {
        if(attempt > 0) {
            heal_team();
            engine_link_up(mac);
            if(!ok(engine_jack_in(0))) break;
        }
        heal_team();
        rooms = 0;
        int guard = 0;
        while(!contains(engine_dungeon_json(), "\"cleared\":true") && guard++ < 40) {
            if(!contains(engine_dungeon_json(), "\"active\":true")) break;
            if(contains(engine_dungeon_json(), "\"roomDone\":true")) {
                CHECK(ok(engine_dungeon_next()));
                continue;
            }
            if(!ok(engine_dungeon_room())) break;
            rooms++;
            if(contains(engine_state_json(), "\"inBattle\":true")) {
                CHECK(contains(engine_battle_json(), "\"canCatch\":false"));
                CHECK(!ok(engine_battle_ball(0)));
                CHECK(!ok(engine_battle_run()));
                fight();
                free(engine_battle_close());
                heal_team();
            }
        }
        if(contains(engine_dungeon_json(), "\"cleared\":true")) break;
    }
    int cleared = contains(engine_dungeon_json(), "\"cleared\":true");
    int still_in = contains(engine_state_json(), "\"inDungeon\":true");
    printf("dungeon: %ld rooms played, %s\n", rooms, cleared ? "cleared" : still_in ? "stuck" : "ejected");
    CHECK(cleared && rooms >= DUNGEON_ROOMS_MIN + 1);
    CHECK(state("dungeonsCleared") == 1);
    CHECK(contains(engine_battle_json(), "\"active\":false"));
    CHECK(!ok(engine_dungeon_next())); /* nothing after the boss */
    CHECK(ok(engine_dungeon_leave()));
    CHECK(contains(engine_state_json(), "\"inDungeon\":false"));
    CHECK(contains(engine_state_json(), "\"wantsLink\":false"));

    /* A dropped connection throws you out. */
    engine_link_up(mac);
    heal_team();
    CHECK(ok(engine_jack_in(0)));
    engine_link_down();
    CHECK(contains(engine_state_json(), "\"inDungeon\":false"));
    CHECK(contains(engine_take_events_json(), "\"kind\":\"ejected\""));
    CHECK(!ok(engine_jack_in(0))); /* not connected any more */
}

int main(void) {
    char dir[] = "/tmp/echoids-engine-XXXXXX";
    if(!mkdtemp(dir)) return 1;
    shim_set_data_dir(dir);
    uint8_t mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    shim_set_device(mac, "Phone");

    CHECK(engine_init(mac, "Phone")); /* new game */
    CHECK(state("team") == 1 && state("pac") == 0 && state("pacCapacity") == 10);

    /* Scanning with the phone's radios */
    CHECK(engine_radios_wanted() == 0);
    engine_scan_set(true);
    CHECK(engine_radios_wanted() == 2);
    /* The app drains sightings several times a second; push in batches. */
    for(int i = 0; i < 400; i++) {
        sighting(0, i);
        if(i % 100 == 99) engine_tick();
    }
    for(int i = 0; i < 300; i++) {
        sighting(1, i);
        if(i % 100 == 99) engine_tick();
    }
    engine_tick();
    CHECK(state("nearby") > 0);
    CHECK(state("shards") > 50);
    CHECK(state("asleep") > 0); /* most access points' echoids are asleep at any hour */
    char* ev = engine_take_events_json();
    CHECK(strstr(ev, "shard") != NULL);
    free(ev);
    CHECK(contains(engine_nearby_json(), "\\\"")); /* SSID quotes escaped */

    /* Same devices again: shards are recharging now. (The cooldown table
     * remembers the last 256 devices, so use recent ones.) */
    long shards_before = state("shardsFound");
    for(int i = 250; i < 300; i++)
        sighting(1, i);
    engine_tick();
    /* A few may have been pushed out of the full table; most are recharging. */
    CHECK(state("shardsFound") - shards_before < 25);
    /* No recharge popups: the shard log lists the device with its time left. */
    CHECK(!contains(engine_take_events_json(), "recharg"));
    CHECK(contains(engine_shards_json(), "\"collected\":true"));
    {
        char* sh = engine_shards_json();
        CHECK(field(sh, "cooling") > 20);
        CHECK(strstr(sh, "\"leftText\":\"2h59m\"") != NULL || strstr(sh, "\"leftText\":\"3h00m\"") != NULL);
        free(sh);
    }

    /* More devices, so there are enough shards for plenty of balls and a few
     * Revive Crystals: a level 1 starter can lose its first fight. */
    for(int i = 1000; i < 2500; i++) {
        sighting(1, i);
        if(i % 100 == 99) engine_tick();
    }
    engine_tick();
    free(engine_take_events_json());

    /* Craft from the shards */
    int crafted = 0;
    for(int i = 0; i < 4; i++)
        free(engine_craft(7)); /* Revive Crystal */
    for(int i = 0; i < 60; i++)
        crafted += ok(engine_craft(0)); /* Echo Ball */
    CHECK(crafted > 5);
    CHECK(contains(engine_inventory_json(), "\"name\":\"Echo Ball\""));

    /* Battle and catch every wild echoid out right now */
    int catches = 0;
    for(int round = 0; round < 40 && state("team") < 6; round++) {
        char* list = engine_nearby_json();
        const char* wild = strstr(list, "\"state\":\"wild\"");
        int idx = -1;
        if(wild) {
            const char* p = wild;
            while(p > list && strncmp(p, "\"index\":", 8) != 0)
                p--;
            idx = (int)strtol(p + 8, NULL, 10);
        }
        free(list);
        if(idx < 0) break;
        if(!ok(engine_battle_begin((uint32_t)idx))) break;
        for(int t = 0; t < 60; t++) {
            char* b = engine_battle_json();
            int over = strstr(b, "\"result\":\"ongoing\"") == NULL;
            int must = strstr(b, "\"mustSwitch\":true") != NULL;
            free(b);
            if(over) break;
            if(must) {
                for(uint8_t k = 0; k < 6; k++)
                    if(ok(engine_battle_switch(k))) break;
                continue;
            }
            if(!ok(engine_battle_ball(0))) ok(engine_battle_move(0));
        }
        char* b = engine_battle_json();
        if(strstr(b, "\"result\":\"caught\"")) catches++;
        CHECK(strstr(b, "\"results\":[") != NULL);
        free(b);
        free(engine_battle_close());
        /* Heal up between fights: revive anyone who fainted (crafting a
         * crystal if needed), then use tonics. */
        for(uint8_t k = 0; k < 6; k++) {
            if(!ok(engine_use_item(ItemRevive, k))) {
                free(engine_craft(7)); /* Revive Crystal */
                free(engine_use_item(ItemRevive, k));
            }
            for(int n = 0; n < 3; n++)
                free(engine_use_item(ItemTonic, k));
        }
    }
    printf("caught %d, team %ld, wins %ld\n", catches, state("team"), state("wins"));
    CHECK(catches > 0 && state("team") == 1 + catches);

    /* Caught echoids leave the list, and don't come back when seen again. */
    CHECK(!contains(engine_nearby_json(), "\"state\":\"owned\""));
    for(int i = 0; i < 400; i++)
        sighting(0, i);
    engine_tick();
    CHECK(!contains(engine_nearby_json(), "\"state\":\"owned\""));
    long nearby_before_flee = state("nearby");

    /* An echoid you ran from stays and can be battled again; one you beat is gone. */
    char fled_mac[18] = {0};
    int beat_fled = 0;
    for(int tries = 0; tries < 20 && !fled_mac[0]; tries++) {
        heal_team();
        char* nl = engine_nearby_json();
        const char* m = strstr(nl, "\"mac\":\"");
        char mac_hex[18] = {0};
        if(m) memcpy(mac_hex, m + 7, 17);
        free(nl);
        if(!mac_hex[0] || !ok(engine_battle_begin(0))) break;
        for(int t = 0; t < 30 && contains(engine_battle_json(), "\"result\":\"ongoing\""); t++)
            free(engine_battle_run());
        int fled = contains(engine_battle_json(), "\"result\":\"fled\"");
        free(engine_battle_close());
        if(fled) memcpy(fled_mac, mac_hex, sizeof(fled_mac));
    }
    CHECK(fled_mac[0]);
    if(fled_mac[0]) {
        CHECK(contains(engine_nearby_json(), fled_mac));
        CHECK(!contains(engine_nearby_json(), "\"state\":\"fought\""));
        CHECK(state("nearby") == nearby_before_flee);
        /* Find it and fight it to the end. */
        char* nl = engine_nearby_json();
        const char* at = strstr(nl, fled_mac);
        const char* p = at;
        while(p && p > nl && strncmp(p, "\"index\":", 8) != 0)
            p--;
        uint32_t idx = p ? (uint32_t)strtol(p + 8, NULL, 10) : 0;
        free(nl);
        long wins_before = state("wins");
        heal_team();
        CHECK(ok(engine_battle_begin(idx)));
        fight();
        int won = contains(engine_battle_json(), "\"result\":\"won\"");
        free(engine_battle_close());
        heal_team();
        beat_fled = won;
        if(won) {
            CHECK(state("wins") == wins_before + 1);
            CHECK(!contains(engine_nearby_json(), fled_mac));
            CHECK(state("nearby") == nearby_before_flee - 1);
            /* Seen again: still gone. */
            for(int i = 0; i < 400; i++)
                sighting(0, i);
            engine_tick();
            CHECK(!contains(engine_nearby_json(), fled_mac));
        } else {
            CHECK(contains(engine_nearby_json(), fled_mac)); /* lost: still out there */
        }
    }

    /* Dungeons */
    test_dungeon();

    /* Team and PAC */
    long team = state("team");
    if(team >= 2) {
        CHECK(ok(engine_team_to_pac(1)));
        CHECK(state("team") == team - 1 && state("pac") == 1);
        CHECK(contains(engine_pac_json(0, 10), "\"index\":0"));
        CHECK(ok(engine_pac_to_team(0)));
        CHECK(state("team") == team && state("pac") == 0);
        CHECK(ok(engine_set_lead(1)) && state("lead") == 1);
    }
    CHECK(contains(engine_echoid_json(0, 0), "\"rating\":{"));

    /* Items: a Revive only works on a fainted echoid */
    CHECK(!ok(engine_use_item(ItemRevive, 0)));

    /* Save, reload, and everything is still there */
    long captures = state("captures"), wins = state("wins");
    engine_unload();
    CHECK(!engine_init(mac, "Phone")); /* loaded, not new */
    CHECK(state("captures") == captures && state("wins") == wins && state("team") == team);
    /* Beaten echoids stay gone after a reload. */
    if(fled_mac[0]) {
        engine_scan_set(true);
        for(int i = 0; i < 400; i++)
            sighting(0, i);
        engine_tick();
        CHECK(contains(engine_nearby_json(), fled_mac) == !beat_fled);
    }

    /* Reset starts over with just the starter */
    CHECK(ok(engine_reset()));
    CHECK(state("team") == 1 && state("captures") == 0);

    CHECK(bad_json == 0);
    printf("%s (%d failure(s))\n", failures ? "FAILED" : "engine OK", failures);
    return failures ? 1 : 0;
}
