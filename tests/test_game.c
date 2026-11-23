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
            battle_attack(&b, (uint8_t)(turns % 4));
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

static void test_shard_cooldown(void) {
    uint32_t hashes[4] = {0}, times[4] = {0}, left = 0;
    uint16_t count = 0;
    CHECK(shard_cooldown_s(ShardDull) < shard_cooldown_s(ShardBright));
    CHECK(shard_cooldown_s(ShardBright) < shard_cooldown_s(ShardPrismatic));

    uint32_t t = 1000000;
    CHECK(shard_claim(hashes, times, &count, 4, 0xAA, ShardDull, t, NULL));
    CHECK(count == 1);
    /* Same device again before the cooldown: refused, remaining time reported */
    CHECK(!shard_claim(hashes, times, &count, 4, 0xAA, ShardDull, t + 60, &left));
    CHECK(left == shard_cooldown_s(ShardDull) - 60);
    /* After the cooldown it can be collected again */
    CHECK(shard_claim(hashes, times, &count, 4, 0xAA, ShardDull, t + shard_cooldown_s(ShardDull), NULL));
    CHECK(count == 1);

    /* A prismatic shard is still recharging when a dull one would be ready */
    CHECK(shard_claim(hashes, times, &count, 4, 0xBB, ShardPrismatic, t, NULL));
    CHECK(!shard_claim(hashes, times, &count, 4, 0xBB, ShardPrismatic, t + shard_cooldown_s(ShardDull), NULL));

    /* Clock going backwards restarts the cooldown instead of unlocking it */
    CHECK(!shard_claim(hashes, times, &count, 4, 0xBB, ShardPrismatic, t - 10, NULL));
    CHECK(!shard_claim(hashes, times, &count, 4, 0xBB, ShardPrismatic, t + 5, NULL));

    /* Full table: the entry collected longest ago is replaced */
    CHECK(shard_claim(hashes, times, &count, 4, 0xCC, ShardDull, t + 2000, NULL));
    CHECK(shard_claim(hashes, times, &count, 4, 0xDD, ShardDull, t + 2001, NULL));
    CHECK(count == 4);
    CHECK(shard_claim(hashes, times, &count, 4, 0xEE, ShardDull, t + 2002, NULL));
    CHECK(count == 4);
    int found_ee = 0, found_bb = 0;
    for(int i = 0; i < 4; i++) {
        found_ee += hashes[i] == 0xEE;
        found_bb += hashes[i] == 0xBB;
    }
    CHECK(found_ee == 1 && found_bb == 0); /* 0xBB (time t-10) was the oldest */

    /* Many devices in the same second: replacements spread over the table
     * instead of all landing in one slot. */
    uint32_t h2[64] = {0}, t2[64] = {0};
    uint16_t c2 = 0;
    for(uint32_t i = 0; i < 64; i++)
        shard_claim(h2, t2, &c2, 64, 1000 + i, ShardDull, 5000, NULL);
    for(uint32_t i = 0; i < 32; i++)
        shard_claim(h2, t2, &c2, 64, 9000 + i * 7919, ShardDull, 5000, NULL);
    int replaced = 0, remembered_new = 0;
    for(int i = 0; i < 64; i++) {
        if(h2[i] < 1000 || h2[i] >= 1064) replaced++;
    }
    for(uint32_t i = 0; i < 32; i++) {
        uint32_t left = 0;
        if(!shard_claim(h2, t2, &c2, 64, 9000 + i * 7919, ShardDull, 5001, &left)) remembered_new++;
    }
    CHECK(replaced > 16); /* not just one churning slot */
    CHECK(remembered_new > 16); /* most new devices are still recharging */
}

static void test_format_duration(void) {
    char b[16];
    format_duration(0, b, sizeof(b));
    CHECK(strcmp(b, "<1m") == 0);
    format_duration(61, b, sizeof(b));
    CHECK(strcmp(b, "2m") == 0);
    format_duration(15 * 60, b, sizeof(b));
    CHECK(strcmp(b, "15m") == 0);
    format_duration(4 * 3600 - 1, b, sizeof(b));
    CHECK(strcmp(b, "4h00m") == 0);
    format_duration(3600 + 5 * 60, b, sizeof(b));
    CHECK(strcmp(b, "1h05m") == 0);
    format_duration(30 * 3600, b, sizeof(b));
    CHECK(strcmp(b, "30h00m") == 0);
    format_duration(300 * 3600, b, sizeof(b));
    CHECK(strcmp(b, "12d12h") == 0);
    format_duration(48 * 3600 + 1, b, sizeof(b));
    CHECK(strcmp(b, "2d01h") == 0);
    /* The shortest cooldown is 3 hours; each rarer tier is ten times longer. */
    CHECK(shard_cooldown_s(ShardDull) == 3 * 3600);
    CHECK(shard_cooldown_s(ShardBright) == 10 * shard_cooldown_s(ShardDull));
    CHECK(shard_cooldown_s(ShardPrismatic) == 10 * shard_cooldown_s(ShardBright));
}

static void test_roster(void) {
    /* Legacy (v4/v5) conversion: lead plus the first five others form the team */
    Echoid all[10];
    memset(all, 0, sizeof(all));
    for(int i = 0; i < 10; i++)
        all[i].mac[5] = (uint8_t)i;
    uint8_t lead = 7, idx[10];
    roster_normalize(all, 10, &lead, true);
    CHECK(roster_party_count(all, 10) == PARTY_MAX);
    CHECK(roster_list(all, 10, true, idx) == 6 && idx[0] == 0 && idx[5] == 7);
    CHECK(roster_list(all, 10, false, idx) == 4 && idx[0] == 5);

    /* Removing team members keeps the lead pointing at the same echoid */
    Echoid team[PARTY_MAX];
    for(int i = 0; i < PARTY_MAX; i++)
        team[i].mac[5] = (uint8_t)(10 + i);
    uint8_t count = PARTY_MAX, l = 3;
    Echoid out;
    CHECK(team_remove(team, &count, &l, 1, &out));
    CHECK(out.mac[5] == 11 && count == 5 && l == 2 && team[l].mac[5] == 13);
    CHECK(team_remove(team, &count, &l, 4, NULL)); /* after the lead: lead unchanged */
    CHECK(l == 2 && team[l].mac[5] == 13);
    CHECK(team_remove(team, &count, &l, 2, NULL)); /* the lead itself: first becomes lead */
    CHECK(l == 0 && count == 3);
    CHECK(team_remove(team, &count, &l, 0, NULL));
    CHECK(team_remove(team, &count, &l, 0, NULL));
    CHECK(count == 1 && !team_remove(team, &count, &l, 0, NULL)); /* never empty */
}

static void test_pac_recipe(void) {
    size_t pac = recipe_count;
    for(size_t i = 0; i < recipe_count; i++)
        if(recipes[i].out == CRAFT_OUT_PAC) pac = i;
    CHECK(pac < recipe_count);
    Inventory inv;
    memset(&inv, 0, sizeof(inv));
    inv.shards[EchoTypeVoid][ShardDull] = 3;
    inv.shards[EchoTypeTide][ShardBright] = 1;
    CHECK(!craft_can(&inv, pac));
    inv.shards[EchoTypeGale][ShardBright] = 1;
    Inventory before = inv;
    CHECK(craft_do(&inv, pac));
    CHECK(inventory_shard_total(&inv) == 0);
    /* a PAC is not a bag item */
    CHECK(memcmp(inv.items, before.items, sizeof(inv.items)) == 0);
}

static void test_moves(void) {
    uint8_t mac[6] = {0x80, 0xE1, 0x26, 0xAA, 0xBB, 0xCC};
    Echoid e;
    Move m[MAX_MOVES];
    EchoGenes g;
    echo_genes(mac, &g);
    echo_init(&e, mac, 1, "x");
    /* Level 1: an own-type move and a move that beats this echoid's counter */
    CHECK(echo_moves(&e, m) == 2);
    CHECK(m[0].type == g.type);
    uint8_t counter = (uint8_t)((g.type + ECHO_TYPE_COUNT - 1) % ECHO_TYPE_COUNT); /* beats g.type */
    CHECK(echo_type_mult(counter, g.type) == 200);
    CHECK(echo_type_mult(m[1].type, counter) == 200); /* the coverage move answers it */
    uint8_t at = 0;
    const Move* next = echo_next_move(&e, &at);
    CHECK(next && at == 8);
    CHECK(echo_move_learned_at(mac, 8) == next);
    CHECK(echo_move_learned_at(mac, 1) == NULL && echo_move_learned_at(mac, 9) == NULL);

    /* Opposed types can never be learned, by any type or species */
    for(uint8_t t = 0; t < ECHO_TYPE_COUNT; t++) {
        CHECK(move_type_learnable(t, t));
        CHECK(!move_type_learnable(t, (uint8_t)((t + 1) % ECHO_TYPE_COUNT)));
        CHECK(!move_type_learnable(t, (uint8_t)((t + ECHO_TYPE_COUNT - 1) % ECHO_TYPE_COUNT)));
        for(uint8_t sp = 0; sp < SPECIES_PER_TYPE; sp++)
            for(uint8_t i = 0; i < learnset_len(); i++)
                CHECK(move_type_learnable(t, learnset_move(t, sp, i, NULL)->type));
    }

    /* Every move of every echoid has a real type */
    for(uint32_t i = 0; i < 2000; i++) {
        uint8_t mm[6] = {1, 2, (uint8_t)(i >> 8), (uint8_t)i, 5, 6};
        Echoid x;
        echo_init(&x, mm, (uint8_t)(1 + i % MAX_LEVEL), "x");
        uint8_t n = echo_moves(&x, m);
        CHECK(n >= 2 && n <= MAX_MOVES);
        for(uint8_t k = 0; k < n; k++)
            CHECK(m[k].type < ECHO_TYPE_COUNT && m[k].power > 0);
    }

    /* Level 16: four moves; level 25: the first is forgotten */
    echo_init(&e, mac, 16, "x");
    CHECK(echo_moves(&e, m) == 4);
    Move first = m[0];
    echo_init(&e, mac, 25, "x");
    CHECK(echo_moves(&e, m) == 4 && strcmp(m[0].name, first.name) != 0);

    /* Level 100: the four latest, and nothing left to learn */
    echo_init(&e, mac, MAX_LEVEL, "x");
    CHECK(echo_moves(&e, m) == 4 && m[3].power == 120);
    CHECK(echo_next_move(&e, NULL) == NULL);

    /* Stats and XP stay inside their types at level 100 */
    EchoStats st;
    echo_stats(&e, &st);
    CHECK(st.max_hp > 300 && st.max_hp < 2000);
    CHECK((uint32_t)echo_xp_to_next(MAX_LEVEL - 1) < 65535u);

    /* Switching: a chosen switch lets the wild echoid hit the newcomer */
    game_rng_seed(7);
    Echoid a, c;
    echo_init(&a, mac, 20, "a");
    uint8_t mac2[6] = {1, 1, 1, 1, 1, 1};
    echo_init(&c, mac2, 20, "c");
    Battle b;
    uint8_t wild[6] = {2, 2, 2, 2, 2, 2};
    battle_start(&b, &a, wild, 20, "w");
    uint16_t hp_before = c.hp;
    battle_switch(&b, &c, true);
    CHECK(b.mine == &c);
    CHECK(c.hp <= hp_before);
    uint16_t a_hp = a.hp;
    battle_switch(&b, &a, false); /* free switch: no attack on a */
    CHECK(b.mine == &a && a.hp == a_hp);
    CHECK(b.fought_n == 2); /* a and c, each counted once */

    /* XP is shared evenly by everyone who fought */
    echo_init(&a, mac, 5, "a");
    echo_init(&c, mac2, 5, "c");
    battle_start(&b, &a, wild, 1, "w");
    battle_switch(&b, &c, false);
    b.wild.hp = 1;
    for(int t = 0; t < 50 && b.result == BattleOngoing; t++) {
        b.wild.hp = 1;
        battle_attack(&b, 0);
        if(b.result == BattleLost) break;
    }
    if(b.result == BattleWon) {
        CHECK(b.fought_xp[0] == b.fought_xp[1]);
        CHECK(b.fought_xp[0] == b.xp_gained / 2);
        CHECK(a.xp > 0 || a.level > 5);
        CHECK(c.xp > 0 || c.level > 5);
    } else {
        CHECK(0 && "test battle should be won");
    }
}

static void test_tonics(void) {
    CHECK(item_heal_amount(ItemTonic) == 20);
    CHECK(item_heal_amount(ItemSuperTonic) == 50);
    CHECK(item_heal_amount(ItemMegaTonic) == 100);
    CHECK(item_heal_amount(ItemMaxTonic) == ITEM_HEAL_FULL);
    CHECK(item_heal_amount(ItemRevive) == 0 && item_heal_amount(ItemEchoBall) == 0);

    /* Every new recipe can be crafted and gives its item */
    uint8_t outs[] = {ItemSuperTonic, ItemMegaTonic, ItemMaxTonic, ItemRevive};
    for(size_t k = 0; k < sizeof(outs); k++) {
        size_t r = recipe_count;
        for(size_t i = 0; i < recipe_count; i++)
            if(recipes[i].out == outs[k]) r = i;
        CHECK(r < recipe_count);
        Inventory inv;
        memset(&inv, 0, sizeof(inv));
        CHECK(!craft_can(&inv, r));
        for(int t = 0; t < ECHO_TYPE_COUNT; t++) {
            inv.shards[t][ShardDull] = 5;
            inv.shards[t][ShardBright] = 3;
            inv.shards[t][ShardPrismatic] = 1;
        }
        CHECK(craft_do(&inv, r) && inv.items[outs[k]] == 1);
    }

    /* A Bright Tide shard isn't wasted on the Tide part while a Dull one exists */
    size_t super = 0;
    for(size_t i = 0; i < recipe_count; i++)
        if(recipes[i].out == ItemSuperTonic) super = i;
    Inventory inv;
    memset(&inv, 0, sizeof(inv));
    inv.shards[EchoTypeTide][ShardDull] = 2;
    inv.shards[EchoTypeFlora][ShardDull] = 2;
    inv.shards[EchoTypeTide][ShardBright] = 1;
    CHECK(craft_do(&inv, super));
    CHECK(inventory_shard_total(&inv) == 0);
}

static void test_names(void) {
    uint8_t mac[6] = {0x80, 0xE1, 0x26, 0xAA, 0xBB, 0xCC};
    char a[ECHO_NAME_LEN], b[ECHO_NAME_LEN];
    echo_name(mac, a, sizeof(a));
    echo_name(mac, b, sizeof(b));
    CHECK(strcmp(a, b) == 0); /* same MAC, same name */

    int distinct = 0;
    static char seen[600][ECHO_NAME_LEN];
    for(uint32_t i = 0; i < 600; i++) {
        uint8_t m[6] = {0x42, 0x13, (uint8_t)(i >> 8), (uint8_t)i, 0x99, 0x01};
        char n[ECHO_NAME_LEN];
        echo_name(m, n, sizeof(n));
        size_t len = strlen(n);
        CHECK(len >= ECHO_NAME_MIN && len <= ECHO_NAME_MAX);
        CHECK(n[0] >= 'A' && n[0] <= 'Z');
        for(size_t k = 1; k < len; k++)
            CHECK(n[k] >= 'a' && n[k] <= 'z');
        /* Never a straight copy of one of its type's source words */
        EchoGenes g;
        echo_genes(m, &g);
        char low[ECHO_NAME_LEN];
        strcpy(low, n);
        low[0] = (char)(low[0] - 'A' + 'a');
        for(const char* const* w = echo_name_corpus(g.type); *w; w++)
            CHECK(strcmp(*w, low) != 0);
        int dup = 0;
        for(int k = 0; k < distinct && !dup; k++)
            dup = strcmp(seen[k], n) == 0;
        if(!dup) strcpy(seen[distinct++], n);
    }
    CHECK(distinct > 450); /* plenty of variety */

    /* Small buffers are truncated safely */
    char tiny[3];
    echo_name(mac, tiny, sizeof(tiny));
    CHECK(strlen(tiny) == 2);
}

static void test_active_hours(void) {
    uint8_t mac[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    CHECK(echo_active_hours(mac) == echo_active_hours(mac)); /* same MAC, same hours */
    uint32_t hours_by_rarity[RarityCount] = {0}, count_by_rarity[RarityCount] = {0};
    uint32_t active_per_hour[24] = {0};
    for(uint32_t i = 0; i < 5000; i++) {
        uint8_t m[6] = {0x33, (uint8_t)(i >> 16), (uint8_t)(i >> 8), (uint8_t)i, 0x44, 0x55};
        uint32_t mask = echo_active_hours(m);
        CHECK(mask != 0 && mask < (1u << 24)); /* out at least one hour, only 24 bits */
        EchoGenes g;
        echo_genes(m, &g);
        uint32_t n = 0;
        for(uint8_t h = 0; h < 24; h++) {
            bool on = (mask >> h) & 1u;
            CHECK(on == echo_active_at(m, h));
            if(on) {
                n++;
                active_per_hour[h]++;
            }
        }
        hours_by_rarity[g.rarity] += n;
        count_by_rarity[g.rarity]++;
    }
    /* Rarer echoids are out for less of the day */
    for(int r = 1; r < RarityCount; r++)
        if(count_by_rarity[r] && count_by_rarity[r - 1])
            CHECK(hours_by_rarity[r] * count_by_rarity[r - 1] <
                  hours_by_rarity[r - 1] * count_by_rarity[r]);
    /* Every hour of the day has echoids about */
    for(int h = 0; h < 24; h++)
        CHECK(active_per_hour[h] > 5000 / 10);
    printf("hours out per day: common %.1f, legendary %.1f\n",
           (double)hours_by_rarity[0] / count_by_rarity[0],
           (double)hours_by_rarity[RarityLegendary] / count_by_rarity[RarityLegendary]);
}

static void test_rating(void) {
    CHECK(stat_stars(STAT_ROLL_MAX, STAT_ROLL_MAX) == 3);
    CHECK(stat_stars(STAT_ROLL_MAX - 1, STAT_ROLL_MAX) == 2);
    CHECK(stat_stars(STAT_ROLL_MAX / 2, STAT_ROLL_MAX) == 2);
    CHECK(stat_stars(STAT_ROLL_MAX / 2 - 1, STAT_ROLL_MAX) == 1);
    CHECK(stat_stars(0, STAT_HP_ROLL_MAX) == 1 && stat_stars(STAT_HP_ROLL_MAX, STAT_HP_ROLL_MAX) == 3);

    uint32_t overall[4] = {0}, perfect_stats = 0, n = 200000;
    for(uint32_t i = 0; i < n; i++) {
        uint8_t m[6] = {0x77, (uint8_t)(i >> 16), (uint8_t)(i >> 8), (uint8_t)i, 0x12, 0x34};
        EchoRating r;
        echo_rating(m, &r);
        EchoGenes g;
        echo_genes(m, &g);
        uint8_t total = 0;
        for(int k = 0; k < STAT_COUNT; k++) {
            CHECK(r.roll[k] <= r.max[k]);
            CHECK(r.stars[k] >= 1 && r.stars[k] <= 3);
            total += r.stars[k];
            if(r.stars[k] == 3) perfect_stats++;
        }
        CHECK(r.total_stars == total);
        /* 3 stars overall means nothing could be stronger */
        CHECK((r.overall == 3) == (total == 12));
        if(r.overall == 3) {
            CHECK(g.base_hp == STAT_HP_FLOOR + STAT_HP_ROLL_MAX + g.rarity * 3);
            CHECK(g.base_atk == STAT_FLOOR + STAT_ROLL_MAX + g.rarity * 2);
        }
        overall[r.overall]++;
    }
    printf("overall stars of %u: 1*=%u 2*=%u 3*=%u (perfect)\n", n, overall[1], overall[2], overall[3]);
    CHECK(overall[1] > 0 && overall[2] > 0);
    CHECK(overall[3] < n / 1000);
}

static void test_type_chart(void) {
    CHECK(echo_type_mult(EchoTypeTide, EchoTypeEmber) == 200);
    CHECK(echo_type_mult(EchoTypeEmber, EchoTypeTide) == 50);
    CHECK(echo_type_mult(EchoTypeVoid, EchoTypeTide) == 200);
    CHECK(echo_type_mult(EchoTypeTide, EchoTypeStone) == 100);
}

static void test_marauder_current(void) {
    ScanEvent ev;
    /* scanall AP line from current Marauder (BSSID unlabeled, beacon bytes appended) */
    CHECK(scanner_parse_line("-61 Ch: 6 a4:2b:b0:11:22:33 ESSID: CoffeeBar 31 04 ", ScanPhaseWifi, &ev));
    CHECK(ev.type == ScanEventAp && ev.rssi == -61 && ev.mac[0] == 0xA4);
    CHECK(strcmp(ev.name, "CoffeeBar") == 0);
    CHECK(scanner_parse_line("-50 Ch: 1 a4:2b:b0:11:22:35 ESSID: My Cafe 24 11 04 ", ScanPhaseWifi, &ev));
    CHECK(strcmp(ev.name, "My Cafe 24") == 0);
    /* hidden network: Marauder prints the BSSID as the name */
    CHECK(scanner_parse_line("-77 Ch: 11 a4:2b:b0:11:22:36 ESSID:  a4:2b:b0:11:22:36 11 04 ", ScanPhaseWifi, &ev));
    CHECK(ev.mac[5] == 0x36 && strcmp(ev.name, "<hidden>") == 0);
    /* station lines from scanall are not creatures */
    CHECK(!scanner_parse_line("3: ap: a4:2b:b0:11:22:33 -> sta: 11:22:33:44:55:66", ScanPhaseWifi, &ev));
    CHECK(!scanner_parse_line("#scanall", ScanPhaseWifi, &ev));
    /* Bluetooth */
    CHECK(scanner_parse_line("-71 Device: 5c:f3:70:aa:bb:cc", ScanPhaseBt, &ev));
    CHECK(ev.type == ScanEventBt && ev.rssi == -71 && ev.mac[0] == 0x5C);
    CHECK(scanner_parse_line("-64 Device: JBL Flip 5", ScanPhaseBt, &ev));
    CHECK(ev.rssi == -64 && strcmp(ev.name, "JBL Flip 5") == 0);

    /* run-together BT records (Marauder without a screen prints no newline) */
    const char* joined = "-71 Device: Buds2-65 Device: ";
    size_t cut = scanner_record_split(joined, strlen(joined));
    CHECK(cut == strlen("-71 Device: Buds2"));
    const char* joined2 = "-71 Device: 5c:f3:70:aa:bb:cc-5 Device: ";
    CHECK(scanner_record_split(joined2, strlen(joined2)) == strlen("-71 Device: 5c:f3:70:aa:bb:cc"));
    CHECK(scanner_record_split("-71 Device: ", 12) == 0);
    CHECK(scanner_record_split("-71 Device: Foo", 15) == 0);
}

static void test_parser(void) {
    ScanEvent ev;
    uint8_t first_ap[6];
    CHECK(scanner_parse_line("RSSI: -61 Ch: 6 BSSID: a4:2b:b0:11:22:33 ESSID: CoffeeBar", ScanPhaseWifi, &ev));
    CHECK(ev.type == ScanEventAp && ev.rssi == -61 && ev.mac[0] == 0xA4 && ev.mac[5] == 0x33);
    CHECK(strcmp(ev.name, "CoffeeBar") == 0);
    CHECK(scanner_parse_line("RSSI: -70 Ch: 1 BSSID: a4:2b:b0:11:22:34 ESSID: ", ScanPhaseWifi, &ev));
    CHECK(strcmp(ev.name, "<hidden>") == 0);
    CHECK(!scanner_parse_line("Starting AP scan. Stop with stopscan", ScanPhaseWifi, &ev));
    /* Marauder builds that print no BSSID still give a stable ID from the SSID */
    CHECK(scanner_parse_line("-58 Ch: 11 ESSID: HomeNet", ScanPhaseWifi, &ev));
    memcpy(first_ap, ev.mac, 6);
    CHECK(ev.type == ScanEventAp && strcmp(ev.name, "HomeNet") == 0);
    CHECK(scanner_parse_line("-70 Ch: 11 ESSID: HomeNet", ScanPhaseWifi, &ev));
    CHECK(memcmp(first_ap, ev.mac, 6) == 0);
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

/* Rules the UIs used to compute themselves, now only in game.c. */
static void test_shared_rules(void) {
    for(uint8_t t = 0; t < ECHO_TYPE_COUNT; t++) {
        uint8_t cant[2];
        echo_unlearnable_types(t, cant);
        for(uint8_t m = 0; m < ECHO_TYPE_COUNT; m++)
            CHECK(move_type_learnable(t, m) == (m != cant[0] && m != cant[1]));
        Move neutral = {"Tackle", MOVE_NEUTRAL, 40, 100};
        CHECK(move_mult(&neutral, t) == 100);
        Move typed = {"Splash", t, 40, 100};
        CHECK(move_mult(&typed, (uint8_t)((t + 1) % ECHO_TYPE_COUNT)) == echo_type_mult(t, (uint8_t)((t + 1) % ECHO_TYPE_COUNT)));
    }
    /* Moves learned over a range of levels match learning them one at a time. */
    uint8_t mac[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    const Move* got[MAX_LEVEL];
    uint8_t n = echo_moves_learned(mac, 1, MAX_LEVEL, got, MAX_LEVEL);
    uint8_t k = 0;
    for(uint16_t lv = 2; lv <= MAX_LEVEL; lv++) {
        const Move* m = echo_move_learned_at(mac, (uint8_t)lv);
        if(m) CHECK(k < n && got[k++] == m);
    }
    CHECK(k == n && n > 0);
    CHECK(echo_moves_learned(mac, 1, MAX_LEVEL, got, 2) == 2);
}

static void test_dungeons(void) {
    unsigned same_type = 0, foes = 0, boss_same = 0, nets = 300;
    for(unsigned n = 0; n < nets; n++) {
        uint8_t mac[6] = {0xA4, 0x12, (uint8_t)(n >> 8), (uint8_t)n, 0x5C, (uint8_t)(n * 7)};
        DungeonLayout a, b;
        dungeon_layout(mac, &a);
        dungeon_layout(mac, &b);
        CHECK(memcmp(&a, &b, sizeof(a)) == 0); /* same network, same dungeon */
        CHECK(a.rooms >= DUNGEON_ROOMS_MIN + 1 && a.rooms <= DUNGEON_ROOMS_MAX + 1);
        CHECK(a.kind[a.rooms - 1] == RoomBoss);
        CHECK(a.kind[0] == RoomFight);
        unsigned rests = 0;
        for(uint8_t r = 0; r < a.rooms; r++) {
            if(a.kind[r] == RoomRest) rests++;
            if(r < a.rooms - 1) CHECK(a.kind[r] != RoomBoss);
            if(a.kind[r] == RoomFight || a.kind[r] == RoomBoss) {
                EchoGenes g;
                echo_genes(a.foe[r], &g);
                if(a.kind[r] == RoomBoss)
                    boss_same += g.type == a.type;
                else {
                    foes++;
                    same_type += g.type == a.type;
                }
            }
        }
        CHECK(rests <= 1);
        char name[NAME_LEN + 12], again[NAME_LEN + 12];
        dungeon_name(mac, name, sizeof(name));
        dungeon_name(mac, again, sizeof(again));
        CHECK(strcmp(name, again) == 0 && strstr(name, dungeon_place_name(a.type)) != NULL);
        uint8_t count, tier;
        dungeon_cache(mac, 2, &count, &tier);
        CHECK(count >= 2 && count <= 4 && tier <= ShardBright);
    }
    /* Foes lean towards the dungeon's element. */
    CHECK(same_type * 100 / foes > 70);
    CHECK(boss_same * 100 / nets > 90);
    CHECK(dungeon_foe_level(10, false) == 10);
    CHECK(dungeon_foe_level(10, true) == 10 + DUNGEON_BOSS_LEVELS);
    CHECK(dungeon_foe_level(MAX_LEVEL, true) == MAX_LEVEL);
    printf("dungeons: %u%% of foes and %u%% of bosses share the element\n", same_type * 100 / foes, boss_same * 100 / nets);
}

static void test_join_parsing(void) {
    int idx = -1;
    char essid[40];
    CHECK(scanner_parse_ap_list("[3][CH:6] My Home Net -60", &idx, essid, sizeof(essid)));
    CHECK(idx == 3 && strcmp(essid, "My Home Net") == 0);
    CHECK(scanner_parse_ap_list("[12][CH:11] Cafe -71 (selected)", &idx, essid, sizeof(essid)));
    CHECK(idx == 12 && strcmp(essid, "Cafe") == 0);
    CHECK(!scanner_parse_ap_list("[0][RSSI:-70] Headphones", &idx, essid, sizeof(essid)));
    CHECK(!scanner_parse_ap_list("-60 Ch: 6 aa:bb:cc:dd:ee:ff ESSID: X", &idx, essid, sizeof(essid)));
    CHECK(scanner_parse_join("IP address: 192.168.1.42") == 1);
    CHECK(scanner_parse_join("Could not connect to WiFi network") == -1);
    CHECK(scanner_parse_join("Connecting to WiFi.....") == 0);
    CHECK(scanner_ssid_matches("Cafe", "Cafe"));
    CHECK(!scanner_ssid_matches("Cafe 5G", "Cafe"));
    CHECK(scanner_ssid_matches("A very long network name", "A very long netwo")); /* ours was cut short */
    CHECK(!scanner_ssid_matches("Anything", ""));
}

static void test_card_mac(void) {
    const uint8_t a[] = {0x04, 0xA1, 0xB2, 0xC3};
    const uint8_t b[] = {0x04, 0xA1, 0xB2, 0xC4};
    uint8_t m1[6], m2[6], m3[6], m4[6], m5[6];
    card_mac(CardNfc, a, sizeof(a), m1);
    card_mac(CardNfc, a, sizeof(a), m2);
    card_mac(CardNfc, b, sizeof(b), m3);
    card_mac(CardRfid, a, sizeof(a), m4);
    card_mac(CardNfc, a, 3, m5);
    CHECK(memcmp(m1, m2, 6) == 0); /* same card, same echoid */
    CHECK(memcmp(m1, m3, 6) != 0);
    CHECK(memcmp(m1, m4, 6) != 0); /* NFC and RFID differ */
    CHECK(memcmp(m1, m5, 6) != 0);
    CHECK((m1[0] & 0x03) == 0x02); /* locally administered, unicast */
    /* Cards spread over the types like access points do. */
    unsigned types[ECHO_TYPE_COUNT] = {0};
    for(uint32_t i = 0; i < 4000; i++) {
        uint8_t uid[4] = {(uint8_t)i, (uint8_t)(i >> 8), 0x55, 0xAA}, m[6];
        EchoGenes g;
        card_mac(CardNfc, uid, sizeof(uid), m);
        echo_genes(m, &g);
        types[g.type]++;
    }
    for(int t = 0; t < ECHO_TYPE_COUNT; t++)
        CHECK(types[t] > 4000 / ECHO_TYPE_COUNT / 2);
}

int main(void) {
    test_card_mac();
    test_join_parsing();
    test_dungeons();
    test_shared_rules();
    test_parser();
    test_marauder_current();
    test_determinism();
    test_distribution();
    test_crafting();
    test_progression_and_battle();
    test_type_chart();
    test_shard_cooldown();
    test_format_duration();
    test_roster();
    test_pac_recipe();
    test_moves();
    test_tonics();
    test_names();
    test_active_hours();
    test_rating();
    if(failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("All tests passed\n");
    return 0;
}
