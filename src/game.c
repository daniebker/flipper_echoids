#include "game.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* RNG                                                                       */
/* ------------------------------------------------------------------------- */

static uint32_t rng_state = 0x9E3779B9u;

void game_rng_seed(uint32_t seed) {
    rng_state = seed ? seed : 0x9E3779B9u;
}

uint32_t game_rand(void) {
    /* xorshift32 */
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

uint32_t game_rand_range(uint32_t n) {
    return n ? game_rand() % n : 0;
}

/* Deterministic stream seeded from a MAC hash. */
typedef struct {
    uint32_t s;
} MacStream;

static uint32_t mac_stream_next(MacStream* st) {
    /* splitmix32-style mixing */
    uint32_t z = (st->s += 0x9E3779B9u);
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    return z ^ (z >> 16);
}

/* ------------------------------------------------------------------------- */
/* Static tables                                                             */
/* ------------------------------------------------------------------------- */

static const char* const type_names[ECHO_TYPE_COUNT] =
    {"Tide", "Ember", "Flora", "Stone", "Spark", "Gale", "Pulse", "Void"};

static const char* const type_short[ECHO_TYPE_COUNT] =
    {"TID", "EMB", "FLO", "STN", "SPK", "GAL", "PLS", "VOI"};

static const char* const skill_names[ECHO_TYPE_COUNT] = {
    "Tidal Lash",
    "Cinder Burst",
    "Thorn Volley",
    "Rock Slam",
    "Arc Bolt",
    "Gust Blade",
    "Ping Wave",
    "Null Bite",
};

#define SPECIES_PER_TYPE 3
static const char* const species_names[ECHO_TYPE_COUNT][SPECIES_PER_TYPE] = {
    {"Drizlet", "Wavern", "Abyssurge"},
    {"Cindrop", "Flarix", "Pyrovane"},
    {"Sproutle", "Thornet", "Verdarch"},
    {"Pebblit", "Graniton", "Monolux"},
    {"Zaplet", "Voltusk", "Arcwyrm"},
    {"Breezel", "Cyclark", "Tempestra"},
    {"Pingle", "Beaconix", "Resonaut"},
    {"Nullpup", "Shadebit", "Eclipson"},
};

static const char* const rarity_names[RarityCount] =
    {"Common", "Uncommon", "Rare", "Epic", "Legendary"};

static const char* const tier_names[SHARD_TIER_COUNT] = {"Dull", "Bright", "Prismatic"};

static const char* const item_names[ItemCount] =
    {"Echo Ball", "Great Ball", "Prism Ball", "Tonic", "Echo Crystal"};

const char* echo_type_name(uint8_t type) {
    return type_names[type % ECHO_TYPE_COUNT];
}
const char* echo_type_short(uint8_t type) {
    return type_short[type % ECHO_TYPE_COUNT];
}
const char* echo_skill_name(uint8_t type) {
    return skill_names[type % ECHO_TYPE_COUNT];
}
const char* echo_rarity_name(uint8_t rarity) {
    return rarity_names[rarity % RarityCount];
}
const char* shard_tier_name(uint8_t tier) {
    return tier_names[tier % SHARD_TIER_COUNT];
}
const char* item_name(uint8_t item) {
    return item < ItemCount ? item_names[item] : "?";
}

/* ------------------------------------------------------------------------- */
/* Generation                                                                */
/* ------------------------------------------------------------------------- */

uint32_t echo_hash_mac(const uint8_t mac[6]) {
    /* FNV-1a over the 6 MAC bytes, then a final avalanche. */
    uint32_t h = 2166136261u;
    for(int i = 0; i < 6; i++) {
        h ^= mac[i];
        h *= 16777619u;
    }
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h;
}

void echo_genes(const uint8_t mac[6], EchoGenes* g) {
    MacStream st = {.s = echo_hash_mac(mac)};
    g->seed = st.s;

    uint32_t r = mac_stream_next(&st);
    g->type = r % ECHO_TYPE_COUNT;

    r = mac_stream_next(&st) % 100;
    if(r < 50)
        g->rarity = RarityCommon;
    else if(r < 78)
        g->rarity = RarityUncommon;
    else if(r < 92)
        g->rarity = RarityRare;
    else if(r < 98)
        g->rarity = RarityEpic;
    else
        g->rarity = RarityLegendary;

    /* Rarer creatures tend to be the evolved-looking species. */
    r = mac_stream_next(&st) % 10;
    uint8_t species = r < 6 ? 0 : (r < 9 ? 1 : 2);
    if(g->rarity >= RarityEpic && species < 2) species++;
    g->species = species;

    uint8_t bonus = g->rarity;
    g->base_hp = 24 + mac_stream_next(&st) % 17 + bonus * 3;
    g->base_atk = 8 + mac_stream_next(&st) % 9 + bonus * 2;
    g->base_def = 8 + mac_stream_next(&st) % 9 + bonus * 2;
    g->base_spd = 8 + mac_stream_next(&st) % 9 + bonus * 2;
}

const char* echo_species_name(const uint8_t mac[6]) {
    EchoGenes g;
    echo_genes(mac, &g);
    return species_names[g.type][g.species];
}

Shard shard_from_mac(const uint8_t mac[6]) {
    /* Salted so that shard element is independent from creature type. */
    uint8_t salted[6];
    for(int i = 0; i < 6; i++)
        salted[i] = mac[i] ^ (uint8_t)(0xA5 + i * 31);
    MacStream st = {.s = echo_hash_mac(salted)};
    Shard s;
    s.type = mac_stream_next(&st) % ECHO_TYPE_COUNT;
    uint32_t r = mac_stream_next(&st) % 100;
    s.tier = r < 70 ? ShardDull : (r < 95 ? ShardBright : ShardPrismatic);
    return s;
}

void echo_sprite(const uint8_t mac[6], uint16_t rows[SPRITE_SIZE]) {
    MacStream st = {.s = echo_hash_mac(mac) ^ 0x5bd1e995u};
    memset(rows, 0, sizeof(uint16_t) * SPRITE_SIZE);
    for(int y = 1; y < SPRITE_SIZE - 1; y++) {
        for(int x = 1; x < 8; x++) {
            /* Denser towards the centre so we get blob-like bodies. */
            int dx = 7 - x;
            int dy = y > 8 ? y - 8 : 8 - y;
            int threshold = 230 - (dx * dx + dy * dy) * 6;
            if(threshold < 20) threshold = 20;
            if((int)(mac_stream_next(&st) & 0xFF) < threshold) {
                rows[y] |= (uint16_t)(1u << (15 - x)); /* left half */
                rows[y] |= (uint16_t)(1u << x); /* mirrored right half */
            }
        }
    }
    /* Eyes: always carve them out of a filled patch so the face reads. */
    int eye_y = 5 + (int)(mac_stream_next(&st) % 3);
    int eye_x = 4 + (int)(mac_stream_next(&st) % 3);
    for(int y = eye_y - 1; y <= eye_y + 1; y++) {
        for(int x = eye_x - 1; x <= eye_x + 1; x++) {
            rows[y] |= (uint16_t)(1u << (15 - x));
            rows[y] |= (uint16_t)(1u << x);
        }
    }
    rows[eye_y] &= (uint16_t)~(1u << (15 - eye_x));
    rows[eye_y] &= (uint16_t)~(1u << eye_x);
}

/* ------------------------------------------------------------------------- */
/* Progression                                                               */
/* ------------------------------------------------------------------------- */

void echo_init(Echoid* e, const uint8_t mac[6], uint8_t level, const char* origin) {
    memset(e, 0, sizeof(*e));
    memcpy(e->mac, mac, 6);
    if(level < 1) level = 1;
    if(level > MAX_LEVEL) level = MAX_LEVEL;
    e->level = level;
    if(origin) {
        strncpy(e->origin, origin, NAME_LEN - 1);
        e->origin[NAME_LEN - 1] = '\0';
    }
    EchoStats s;
    echo_stats(e, &s);
    e->hp = s.max_hp;
}

static uint16_t grow(uint8_t base, uint8_t level, uint8_t per_level) {
    uint32_t l = level > 0 ? level - 1u : 0u;
    return (uint16_t)(base + (base * l) / 8u + per_level * l);
}

void echo_stats(const Echoid* e, EchoStats* out) {
    EchoGenes g;
    echo_genes(e->mac, &g);
    out->max_hp = grow(g.base_hp, e->level, 2);
    out->atk = grow(g.base_atk, e->level, 1);
    out->def = grow(g.base_def, e->level, 1);
    out->spd = grow(g.base_spd, e->level, 1);
}

uint16_t echo_xp_to_next(uint8_t level) {
    return (uint16_t)(10u + (uint32_t)level * level * 4u);
}

uint8_t echo_add_xp(Echoid* e, uint32_t xp) {
    uint8_t gained = 0;
    if(e->level >= MAX_LEVEL) return 0;
    uint32_t total = e->xp + xp;
    while(e->level < MAX_LEVEL && total >= echo_xp_to_next(e->level)) {
        EchoStats before;
        echo_stats(e, &before);
        total -= echo_xp_to_next(e->level);
        e->level++;
        gained++;
        EchoStats after;
        echo_stats(e, &after);
        /* Levelling up also restores the HP gained. */
        e->hp += after.max_hp - before.max_hp;
        if(e->hp > after.max_hp) e->hp = after.max_hp;
    }
    if(e->level >= MAX_LEVEL) total = 0;
    e->xp = (uint16_t)total;
    return gained;
}

uint8_t echo_wild_level(const uint8_t mac[6], uint8_t lead_level) {
    EchoGenes g;
    echo_genes(mac, &g);
    int offset = (int)(g.seed % 4u) - 2 + (g.rarity >= RarityRare ? 1 : 0); /* -2 .. +2 */
    int level = (int)lead_level + offset;
    if(level < 1) level = 1;
    if(level > MAX_LEVEL) level = MAX_LEVEL;
    return (uint8_t)level;
}

uint8_t echo_type_mult(uint8_t attacker, uint8_t defender) {
    attacker %= ECHO_TYPE_COUNT;
    defender %= ECHO_TYPE_COUNT;
    if(defender == (attacker + 1) % ECHO_TYPE_COUNT) return 200;
    if(attacker == (defender + 1) % ECHO_TYPE_COUNT) return 50;
    return 100;
}

static const uint8_t capture_base[RarityCount] = {60, 45, 30, 18, 8};
static const uint8_t ball_mult10[3] = {10, 15, 25};

uint8_t echo_capture_chance(const Echoid* wild, uint8_t ball) {
    if(ball > ItemPrismBall) return 0;
    EchoGenes g;
    echo_genes(wild->mac, &g);
    EchoStats s;
    echo_stats(wild, &s);
    uint32_t max3 = (uint32_t)s.max_hp * 3u;
    uint32_t hp2 = (uint32_t)wild->hp * 2u;
    uint32_t p = (uint32_t)capture_base[g.rarity] * ball_mult10[ball] * (max3 - hp2) / max3 / 10u;
    if(p < 1) p = 1;
    if(p > 95) p = 95;
    return (uint8_t)p;
}

/* ------------------------------------------------------------------------- */
/* Crafting                                                                  */
/* ------------------------------------------------------------------------- */

const Recipe recipes[] = {
    {"Echo Ball", "3 Dull shards", ItemEchoBall, {3, 0, 0}, -1, 0, -1, 0, 0},
    {"Great Ball", "2 Bright + 1 Dull", ItemGreatBall, {1, 2, 0}, -1, 0, -1, 0, 0},
    {"Prism Ball", "1 Prismatic + 2 Bright", ItemPrismBall, {0, 2, 1}, -1, 0, -1, 0, 0},
    {"Tonic", "2 Tide + 1 Flora", ItemTonic, {0, 0, 0}, EchoTypeTide, 2, EchoTypeFlora, 1, 0},
    {"Echo Crystal", "5 shards of any kind", ItemCrystal, {0, 0, 0}, -1, 0, -1, 0, 5},
};
const size_t recipe_count = sizeof(recipes) / sizeof(recipes[0]);

uint32_t inventory_shard_total(const Inventory* inv) {
    uint32_t total = 0;
    for(int t = 0; t < ECHO_TYPE_COUNT; t++)
        for(int k = 0; k < SHARD_TIER_COUNT; k++)
            total += inv->shards[t][k];
    return total;
}

static const uint32_t shard_cooldowns[SHARD_TIER_COUNT] = {
    15u * 60u, /* Dull: 15 minutes */
    60u * 60u, /* Bright: 1 hour */
    4u * 60u * 60u, /* Prismatic: 4 hours */
};

void format_duration(uint32_t seconds, char* out, size_t size) {
    uint32_t minutes = (seconds + 59u) / 60u; /* round up: never show 0m left */
    if(seconds < 60u)
        snprintf(out, size, "<1m");
    else if(minutes < 60u)
        snprintf(out, size, "%lum", (unsigned long)minutes);
    else
        snprintf(out, size, "%luh%02lum", (unsigned long)(minutes / 60u), (unsigned long)(minutes % 60u));
}

uint32_t shard_cooldown_s(uint8_t tier) {
    return shard_cooldowns[tier % SHARD_TIER_COUNT];
}

bool shard_claim(
    uint32_t* hashes,
    uint32_t* times,
    uint16_t* count,
    uint16_t max,
    uint32_t hash,
    uint8_t tier,
    uint32_t now,
    uint32_t* remaining_s) {
    uint32_t cooldown = shard_cooldown_s(tier);
    if(remaining_s) *remaining_s = 0;

    for(uint16_t i = 0; i < *count; i++) {
        if(hashes[i] != hash) continue;
        if(now < times[i]) {
            /* Clock went backwards: restart the cooldown rather than unlock it. */
            times[i] = now;
            if(remaining_s) *remaining_s = cooldown;
            return false;
        }
        uint32_t elapsed = now - times[i];
        if(elapsed < cooldown) {
            if(remaining_s) *remaining_s = cooldown - elapsed;
            return false;
        }
        times[i] = now;
        return true;
    }

    /* New device: use a free slot, else the entry collected longest ago. */
    uint16_t slot;
    if(*count < max) {
        slot = (*count)++;
    } else {
        slot = 0;
        for(uint16_t i = 1; i < max; i++)
            if(times[i] < times[slot]) slot = i;
    }
    hashes[slot] = hash;
    times[slot] = now;
    return true;
}

/* Take n shards of a tier from whichever element has the most. */
static bool take_tier(Inventory* inv, uint8_t tier, uint8_t n) {
    while(n--) {
        int best = -1;
        for(int t = 0; t < ECHO_TYPE_COUNT; t++) {
            if(inv->shards[t][tier] && (best < 0 || inv->shards[t][tier] > inv->shards[best][tier]))
                best = t;
        }
        if(best < 0) return false;
        inv->shards[best][tier]--;
    }
    return true;
}

/* Take n shards of an element, cheapest tier first. */
static bool take_elem(Inventory* inv, uint8_t elem, uint8_t n) {
    while(n--) {
        int tier = -1;
        for(int k = 0; k < SHARD_TIER_COUNT; k++) {
            if(inv->shards[elem][k]) {
                tier = k;
                break;
            }
        }
        if(tier < 0) return false;
        inv->shards[elem][tier]--;
    }
    return true;
}

static bool take_any(Inventory* inv, uint8_t n) {
    while(n--) {
        bool ok = false;
        for(int k = 0; k < SHARD_TIER_COUNT && !ok; k++)
            ok = take_tier(inv, (uint8_t)k, 1);
        if(!ok) return false;
    }
    return true;
}

static bool craft_apply(Inventory* inv, const Recipe* r) {
    for(int k = SHARD_TIER_COUNT - 1; k >= 0; k--)
        if(!take_tier(inv, (uint8_t)k, r->tier_need[k])) return false;
    if(r->elem_a >= 0 && !take_elem(inv, (uint8_t)r->elem_a, r->elem_a_n)) return false;
    if(r->elem_b >= 0 && !take_elem(inv, (uint8_t)r->elem_b, r->elem_b_n)) return false;
    if(!take_any(inv, r->any_n)) return false;
    return true;
}

bool craft_can(const Inventory* inv, size_t recipe) {
    if(recipe >= recipe_count) return false;
    Inventory tmp = *inv;
    return craft_apply(&tmp, &recipes[recipe]);
}

bool craft_do(Inventory* inv, size_t recipe) {
    if(recipe >= recipe_count) return false;
    Inventory tmp = *inv;
    if(!craft_apply(&tmp, &recipes[recipe])) return false;
    if(tmp.items[recipes[recipe].out] < 999) tmp.items[recipes[recipe].out]++;
    *inv = tmp;
    return true;
}

/* ------------------------------------------------------------------------- */
/* Battle                                                                    */
/* ------------------------------------------------------------------------- */

static void battle_log(Battle* b, const char* msg) {
    memmove(b->log[0], b->log[1], BATTLE_LOG_LEN);
    strncpy(b->log[BATTLE_LOG_LINES - 1], msg, BATTLE_LOG_LEN - 1);
    b->log[BATTLE_LOG_LINES - 1][BATTLE_LOG_LEN - 1] = '\0';
}

void battle_start(Battle* b, Echoid* mine, const uint8_t mac[6], uint8_t level, const char* ssid) {
    memset(b, 0, sizeof(*b));
    b->mine = mine;
    echo_init(&b->wild, mac, level, ssid);
    b->result = BattleOngoing;
    char msg[BATTLE_LOG_LEN];
    snprintf(msg, sizeof(msg), "A wild %s appears!", echo_species_name(mac));
    battle_log(b, msg);
}

/* Performs one attack. Returns true if the defender fainted. */
static bool do_attack(Battle* b, Echoid* atk, Echoid* def, bool skill, bool is_player) {
    EchoGenes ga, gd;
    echo_genes(atk->mac, &ga);
    echo_genes(def->mac, &gd);
    EchoStats sa, sd;
    echo_stats(atk, &sa);
    echo_stats(def, &sd);

    const char* who = is_player ? "You" : "Foe";
    char msg[BATTLE_LOG_LEN];

    uint8_t power = skill ? 65 : 40;
    uint8_t accuracy = skill ? 85 : 100;
    if(game_rand_range(100) >= accuracy) {
        snprintf(msg, sizeof(msg), "%s %s missed", who, skill ? echo_skill_name(ga.type) : "Strike");
        battle_log(b, msg);
        return false;
    }

    uint32_t dmg = ((2u * atk->level / 5u + 2u) * power * sa.atk / (sd.def ? sd.def : 1)) / 26u + 2u;
    uint8_t mult = skill ? echo_type_mult(ga.type, gd.type) : 100;
    dmg = dmg * mult / 100u;
    dmg = dmg * (85u + game_rand_range(16)) / 100u;
    if(dmg < 1) dmg = 1;

    def->hp = dmg >= def->hp ? 0 : (uint16_t)(def->hp - dmg);

    const char* eff = mult > 100 ? " x2!" : (mult < 100 ? " x.5" : "");
    snprintf(
        msg,
        sizeof(msg),
        "%s %s -%lu%s",
        who,
        skill ? echo_skill_name(ga.type) : "Strike",
        (unsigned long)dmg,
        eff);
    battle_log(b, msg);
    return def->hp == 0;
}

static void award_xp(Battle* b, bool captured) {
    EchoGenes g;
    echo_genes(b->wild.mac, &g);
    uint32_t xp = 8u + b->wild.level * 5u;
    xp = xp * (100u + 25u * g.rarity) / 100u;
    if(captured) xp = xp * 3u / 2u;
    b->xp_gained = xp;
    b->levels_gained = echo_add_xp(b->mine, xp);
}

static void enemy_turn(Battle* b) {
    if(b->result != BattleOngoing) return;
    bool skill = game_rand_range(100) < 40;
    if(do_attack(b, &b->wild, b->mine, skill, false)) {
        b->result = BattleLost;
        battle_log(b, "Your echoid fainted!");
    }
}

static void player_attack(Battle* b, bool skill) {
    if(b->result != BattleOngoing) return;
    if(do_attack(b, b->mine, &b->wild, skill, true)) {
        b->result = BattleWon;
        award_xp(b, false);
        battle_log(b, "The wild echoid fled!");
    }
}

void battle_attack(Battle* b, bool skill) {
    if(b->result != BattleOngoing) return;
    EchoStats sm, sw;
    echo_stats(b->mine, &sm);
    echo_stats(&b->wild, &sw);
    if(sm.spd >= sw.spd) {
        player_attack(b, skill);
        enemy_turn(b);
    } else {
        enemy_turn(b);
        player_attack(b, skill);
    }
}

bool battle_throw(Battle* b, Inventory* inv, uint8_t ball) {
    if(b->result != BattleOngoing) return false;
    if(ball > ItemPrismBall || inv->items[ball] == 0) return false;
    inv->items[ball]--;
    uint8_t chance = echo_capture_chance(&b->wild, ball);
    char msg[BATTLE_LOG_LEN];
    if(game_rand_range(100) < chance) {
        b->result = BattleCaptured;
        snprintf(msg, sizeof(msg), "Gotcha! %s caught!", echo_species_name(b->wild.mac));
        battle_log(b, msg);
        award_xp(b, true);
    } else {
        snprintf(msg, sizeof(msg), "It broke free! (%u%%)", chance);
        battle_log(b, msg);
        enemy_turn(b);
    }
    return true;
}

void battle_flee(Battle* b) {
    if(b->result != BattleOngoing) return;
    EchoStats sm, sw;
    echo_stats(b->mine, &sm);
    echo_stats(&b->wild, &sw);
    int chance = 50 + ((int)sm.spd - (int)sw.spd) * 5;
    if(chance < 20) chance = 20;
    if(chance > 95) chance = 95;
    if((int)game_rand_range(100) < chance) {
        b->result = BattleFled;
        battle_log(b, "Got away safely.");
    } else {
        battle_log(b, "Couldn't escape!");
        enemy_turn(b);
    }
}
