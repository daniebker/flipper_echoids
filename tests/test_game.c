/* Host-side tests for the deterministic game rules.
 * Build & run:  cc -std=c11 -Wall -Wextra -Isrc src/game.c src/scanner_parse.c tests/test_game.c -o /tmp/t && /tmp/t
 */
#include "game.h"
#include "scanner.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if(!(c)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);     \
            failures++;                                             \
        }                                                           \
    } while(0)

static void test_determinism(void) {
    uint8_t mac[6] = {0x80, 0xE1, 0x26, 0x12, 0x34, 0x56};
    EchoGenes a, b;
    echo_genes(mac, &a);
    echo_genes(mac, &b);
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    CHECK(strcmp(echo_species_name(mac), echo_species_name(mac)) == 0);

    Shard s1 = shard_from_mac(mac), s2 = shard_from_mac(mac);
    CHECK(s1.type == s2.type && s1.tier == s2.tier);

    uint16_t r1[SPRITE_SIZE], r2[SPRITE_SIZE];
    echo_sprite(mac, r1);
    echo_sprite(mac, r2);
    CHECK(memcmp(r1, r2, sizeof(r1)) == 0);

    uint8_t mac2[6] = {0x80, 0xE1, 0x26, 0x12, 0x34, 0x57};
    EchoGenes c;
    echo_genes(mac2, &c);
    CHECK(c.seed != a.seed);
}

static void test_distribution(void) {
    uint32_t rarity[RarityCount] = {0}, type[ECHO_TYPE_COUNT] = {0}, tier[SHARD_TIER_COUNT] = {0};
    for(uint32_t i = 0; i < 20000; i++) {
        uint8_t mac[6] = {0x00, 0x11, (uint8_t)(i >> 16), (uint8_t)(i >> 8), (uint8_t)i, 0x42};
        EchoGenes g;
        echo_genes(mac, &g);
        rarity[g.rarity]++;
        type[g.type]++;
        tier[shard_from_mac(mac).tier]++;
    }
    printf("rarity:");
    for(int i = 0; i < RarityCount; i++) printf(" %s=%u", echo_rarity_name(i), rarity[i]);
    printf("\ntypes :");
    for(int i = 0; i < ECHO_TYPE_COUNT; i++) printf(" %s=%u", echo_type_name(i), type[i]);
    printf("\ntiers :");
    for(int i = 0; i < SHARD_TIER_COUNT; i++) printf(" %s=%u", shard_tier_name(i), tier[i]);
    printf("\n");
    for(int i = 0; i < ECHO_TYPE_COUNT; i++) CHECK(type[i] > 2000);
    CHECK(rarity[RarityLegendary] > 100 && rarity[RarityLegendary] < 1000);
}

static void test_crafting(void) {
    Inventory inv;
    memset(&inv, 0, sizeof(inv));
    CHECK(!craft_can(&inv, 0));
    inv.shards[EchoTypeTide][ShardDull] = 2;
    inv.shards[EchoTypeEmber][ShardDull] = 1;
    CHECK(craft_can(&inv, 0));
    CHECK(craft_do(&inv, 0));
    CHECK(inv.items[ItemEchoBall] == 1);
    CHECK(inventory_shard_total(&inv) == 0);

    /* Tonic needs specific elements */
    inv.shards[EchoTypeTide][ShardBright] = 2;
    CHECK(!craft_can(&inv, 3));
    inv.shards[EchoTypeFlora][ShardDull] = 1;
    CHECK(craft_do(&inv, 3));
    CHECK(inv.items[ItemTonic] == 1);

    /* Failed craft must not consume anything */
    memset(&inv, 0, sizeof(inv));
    inv.shards[EchoTypeVoid][ShardBright] = 2;
    CHECK(!craft_do(&inv, 2));
    CHECK(inv.shards[EchoTypeVoid][ShardBright] == 2);
    inv.shards[EchoTypeGale][ShardPrismatic] = 1;
    CHECK(craft_do(&inv, 2));
    CHECK(inv.items[ItemPrismBall] == 1 && inventory_shard_total(&inv) == 0);
}

static void test_progression_and_battle(void) {
    uint8_t mine_mac[6] = {0x80, 0xE1, 0x26, 0xAA, 0xBB, 0xCC};
    Echoid me;
    echo_init(&me, mine_mac, 1, "Flipper");
    EchoStats s;
    echo_stats(&me, &s);
    CHECK(me.level == 1 && me.hp == s.max_hp && me.xp == 0);
    EchoGenes sg;
    echo_genes(mine_mac, &sg);
    printf("starter: %s (%s, %s) hp=%u atk=%u def=%u spd=%u\n",
           echo_species_name(mine_mac), echo_type_name(sg.type), echo_rarity_name(sg.rarity),
           s.max_hp, s.atk, s.def, s.spd);

    game_rng_seed(1234);
    uint32_t wins = 0, losses = 0, caught = 0, turns_total = 0;
    for(int i = 0; i < 300; i++) {
        Echoid hero;
        echo_init(&hero, mine_mac, 5, "Flipper");
        uint8_t wild_mac[6] = {0x10, 0x20, 0x30, 0x40, (uint8_t)(i >> 8), (uint8_t)i};
        Battle b;
        battle_start(&b, &hero, wild_mac, echo_wild_level(wild_mac, hero.level), "Net");
        int turns = 0;
        while(b.result == BattleOngoing && turns < 100) {
            battle_attack(&b, (turns & 1) != 0);
            turns++;
        }
        turns_total += turns;
        if(b.result == BattleWon) {
            wins++;
            CHECK(b.xp_gained > 0);
        } else if(b.result == BattleLost) {
            losses++;
        }
        /* Capture attempts from a weakened state */
        Inventory inv;
        memset(&inv, 0, sizeof(inv));
        inv.items[ItemGreatBall] = 5;
        echo_init(&hero, mine_mac, 5, "Flipper");
        battle_start(&b, &hero, wild_mac, 5, "Net");
        b.wild.hp = 1 + b.wild.hp / 4;
        while(b.result == BattleOngoing && battle_throw(&b, &inv, ItemGreatBall)) {
        }
        if(b.result == BattleCaptured) caught++;
    }
    printf("300 fights at Lv5: won=%u lost=%u avg_turns=%u, captured(weak, 5 great balls)=%u\n",
           wins, losses, turns_total / 300, caught);
    CHECK(wins > 60);
    CHECK(turns_total / 300 >= 2);

    Echoid lv;
    echo_init(&lv, mine_mac, 1, "x");
    uint8_t g = echo_add_xp(&lv, 1000);
    CHECK(g > 0 && lv.level == 1 + g);
    CHECK(lv.xp < echo_xp_to_next(lv.level));
    echo_add_xp(&lv, 10000000);
    CHECK(lv.level == MAX_LEVEL);
}

static void test_type_chart(void) {
    CHECK(echo_type_mult(EchoTypeTide, EchoTypeEmber) == 200);
    CHECK(echo_type_mult(EchoTypeEmber, EchoTypeTide) == 50);
    CHECK(echo_type_mult(EchoTypeVoid, EchoTypeTide) == 200);
    CHECK(echo_type_mult(EchoTypeTide, EchoTypeStone) == 100);
}

static void test_parser(void) {
    ScanEvent ev;
    CHECK(scanner_parse_line("RSSI: -61 Ch: 6 BSSID: a4:2b:b0:11:22:33 ESSID: CoffeeBar", ScanPhaseWifi, &ev));
    CHECK(ev.type == ScanEventAp && ev.rssi == -61 && ev.mac[0] == 0xA4 && ev.mac[5] == 0x33);
    CHECK(strcmp(ev.name, "CoffeeBar") == 0);
    CHECK(scanner_parse_line("RSSI: -70 Ch: 1 BSSID: a4:2b:b0:11:22:34 ESSID: ", ScanPhaseWifi, &ev));
    CHECK(strcmp(ev.name, "<hidden>") == 0);
    CHECK(!scanner_parse_line("Starting AP scan. Stop with stopscan", ScanPhaseWifi, &ev));
    CHECK(scanner_parse_line("Device: 5c:f3:70:aa:bb:cc", ScanPhaseBt, &ev));
    CHECK(ev.type == ScanEventBt && ev.mac[0] == 0x5C);
    uint8_t first[6];
    CHECK(scanner_parse_line("RSSI: -50 Device: Galaxy Buds", ScanPhaseBt, &ev));
    memcpy(first, ev.mac, 6);
    CHECK(scanner_parse_line("RSSI: -80 Device: Galaxy Buds", ScanPhaseBt, &ev));
    CHECK(memcmp(first, ev.mac, 6) == 0 && strcmp(ev.name, "Galaxy Buds") == 0);
    CHECK(scanner_parse_line("BT,11:22:33:44:55:66,-42,Watch", ScanPhaseWifi, &ev));
    CHECK(ev.type == ScanEventBt && ev.rssi == -42 && strcmp(ev.name, "Watch") == 0);
    CHECK(scanner_parse_line("AP,11-22-33-44-55-67,-55,Home", ScanPhaseBt, &ev));
    CHECK(ev.type == ScanEventAp && ev.mac[5] == 0x67);
}

int main(void) {
    test_parser();
    test_determinism();
    test_distribution();
    test_crafting();
    test_progression_and_battle();
    test_type_chart();
    if(failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("All tests passed\n");
    return 0;
}
