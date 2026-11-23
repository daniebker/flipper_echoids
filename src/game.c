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

/* ---- Moves ---- */

/* Six moves per type (TYPED_MOVES), weakest first. */
static const Move typed_moves[ECHO_TYPE_COUNT][TYPED_MOVES] = {
    {{"Splash Jab", EchoTypeTide, 45, 100},
     {"Bubble Shot", EchoTypeTide, 55, 95},
     {"Tidal Lash", EchoTypeTide, 70, 90},
     {"Riptide", EchoTypeTide, 85, 85},
     {"Maelstrom", EchoTypeTide, 100, 85},
     {"Tsunami", EchoTypeTide, 120, 75}},
    {{"Cinder Flick", EchoTypeEmber, 45, 100},
     {"Flame Lick", EchoTypeEmber, 55, 95},
     {"Cinder Burst", EchoTypeEmber, 70, 90},
     {"Blaze Rush", EchoTypeEmber, 85, 85},
     {"Inferno", EchoTypeEmber, 100, 85},
     {"Supernova", EchoTypeEmber, 120, 75}},
    {{"Vine Tap", EchoTypeFlora, 45, 100},
     {"Seed Spit", EchoTypeFlora, 55, 95},
     {"Thorn Volley", EchoTypeFlora, 70, 90},
     {"Root Crush", EchoTypeFlora, 85, 85},
     {"Bloom Storm", EchoTypeFlora, 100, 85},
     {"World Tree", EchoTypeFlora, 120, 75}},
    {{"Pebble Toss", EchoTypeStone, 45, 100},
     {"Rock Throw", EchoTypeStone, 55, 95},
     {"Rock Slam", EchoTypeStone, 70, 90},
     {"Quake", EchoTypeStone, 85, 85},
     {"Landslide", EchoTypeStone, 100, 85},
     {"Tectonic", EchoTypeStone, 120, 75}},
    {{"Zap", EchoTypeSpark, 45, 100},
     {"Shock Dart", EchoTypeSpark, 55, 95},
     {"Arc Bolt", EchoTypeSpark, 70, 90},
     {"Thunderclap", EchoTypeSpark, 85, 85},
     {"Plasma Lance", EchoTypeSpark, 100, 85},
     {"Megavolt", EchoTypeSpark, 120, 75}},
    {{"Breeze", EchoTypeGale, 45, 100},
     {"Air Slice", EchoTypeGale, 55, 95},
     {"Gust Blade", EchoTypeGale, 70, 90},
     {"Cyclone", EchoTypeGale, 85, 85},
     {"Tempest", EchoTypeGale, 100, 85},
     {"Jetstream", EchoTypeGale, 120, 75}},
    {{"Ping", EchoTypePulse, 45, 100},
     {"Echo Beam", EchoTypePulse, 55, 95},
     {"Ping Wave", EchoTypePulse, 70, 90},
     {"Resonance", EchoTypePulse, 85, 85},
     {"Feedback", EchoTypePulse, 100, 85},
     {"Sonic Rupture", EchoTypePulse, 120, 75}},
    {{"Shade Nip", EchoTypeVoid, 45, 100},
     {"Null Bite", EchoTypeVoid, 55, 95},
     {"Gloom Claw", EchoTypeVoid, 70, 90},
     {"Eclipse Fang", EchoTypeVoid, 85, 85},
     {"Abyss Gaze", EchoTypeVoid, 100, 85},
     {"Event Horizon", EchoTypeVoid, 120, 75}},
};

/* Where a learned move comes from: the echoid's own type, or a coverage type. */
typedef enum {
    LearnOwn, /* own type */
    LearnCounter, /* the type that beats whatever beats this echoid */
    LearnSpecies, /* a type that depends on the species */
} LearnFrom;

typedef struct {
    uint8_t level;
    uint8_t from; /* LearnFrom */
    uint8_t move; /* index into that type's six moves */
} LearnStep;

static const LearnStep learnset[] = {
    {1, LearnOwn, 0},
    {1, LearnCounter, 0},
    {8, LearnOwn, 1},
    {16, LearnSpecies, 2},
    {25, LearnOwn, 2},
    {38, LearnOwn, 3},
    {52, LearnCounter, 4},
    {66, LearnOwn, 4},
    {82, LearnOwn, 5},
};
#define LEARNSET_LEN (sizeof(learnset) / sizeof(learnset[0]))

/* Type t is beaten by t-1 (in the cycle), which is beaten by t-2. */
static uint8_t counter_type(uint8_t type) {
    return (uint8_t)((type + ECHO_TYPE_COUNT - 2) % ECHO_TYPE_COUNT);
}

/* Species 0..2 of a type learn a move of type t+3, t+4 or t+5. */
static uint8_t species_type(const EchoGenes* g) {
    return (uint8_t)((g->type + 3 + g->species) % ECHO_TYPE_COUNT);
}

bool move_type_learnable(uint8_t echo_type, uint8_t move_type) {
    echo_type %= ECHO_TYPE_COUNT;
    move_type %= ECHO_TYPE_COUNT;
    /* Opposed elements: never the type it beats, nor the type that beats it. */
    if(move_type == (echo_type + 1) % ECHO_TYPE_COUNT) return false;
    if(echo_type == (move_type + 1) % ECHO_TYPE_COUNT) return false;
    return true;
}

static const Move* learn_step_move(const EchoGenes* g, const LearnStep* st) {
    uint8_t t = g->type;
    if(st->from == LearnCounter) t = counter_type(g->type);
    if(st->from == LearnSpecies) t = species_type(g);
    /* Safety net: an opposed type falls back to the echoid's own type. */
    if(!move_type_learnable(g->type, t)) t = g->type;
    return &typed_moves[t % ECHO_TYPE_COUNT][st->move];
}

const Move* move_get(uint8_t type, uint8_t idx) {
    if(type >= ECHO_TYPE_COUNT || idx >= TYPED_MOVES) return NULL;
    return &typed_moves[type][idx];
}

uint8_t learnset_len(void) {
    return (uint8_t)LEARNSET_LEN;
}

const Move* learnset_move(uint8_t type, uint8_t species, uint8_t i, uint8_t* level) {
    if(i >= LEARNSET_LEN) return NULL;
    EchoGenes g;
    memset(&g, 0, sizeof(g));
    g.type = type % ECHO_TYPE_COUNT;
    g.species = species;
    if(level) *level = learnset[i].level;
    return learn_step_move(&g, &learnset[i]);
}

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

static const char* const item_names[ItemCount] = {
    "Echo Ball",
    "Great Ball",
    "Prism Ball",
    "Tonic",
    "Echo Crystal",
    "Super Tonic",
    "Mega Tonic",
    "Max Tonic",
    "Revive Crystal",
};

_Static_assert(ItemCount <= ITEM_SLOTS, "more items than saved item slots");

uint16_t item_heal_amount(uint8_t item) {
    switch(item) {
    case ItemTonic:
        return 20;
    case ItemSuperTonic:
        return 50;
    case ItemMegaTonic:
        return 100;
    case ItemMaxTonic:
        return ITEM_HEAL_FULL;
    default:
        return 0;
    }
}

const char* echo_type_name(uint8_t type) {
    return type_names[type % ECHO_TYPE_COUNT];
}
const char* echo_type_short(uint8_t type) {
    return type_short[type % ECHO_TYPE_COUNT];
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

const char* species_name_of(uint8_t type, uint8_t species) {
    return species_names[type % ECHO_TYPE_COUNT][species % SPECIES_PER_TYPE];
}

const char* echo_species_name(const uint8_t mac[6]) {
    EchoGenes g;
    echo_genes(mac, &g);
    return species_names[g.type][g.species];
}

uint8_t echo_moves(const Echoid* e, Move out[MAX_MOVES]) {
    EchoGenes g;
    echo_genes(e->mac, &g);
    size_t learned = 0;
    while(learned < LEARNSET_LEN && learnset[learned].level <= e->level)
        learned++;
    size_t first = learned > MAX_MOVES ? learned - MAX_MOVES : 0;
    uint8_t n = 0;
    for(size_t i = first; i < learned; i++)
        out[n++] = *learn_step_move(&g, &learnset[i]);
    return n;
}

const Move* echo_move_learned_at(const uint8_t mac[6], uint8_t level) {
    if(level <= 1) return NULL;
    EchoGenes g;
    echo_genes(mac, &g);
    for(size_t i = 0; i < LEARNSET_LEN; i++)
        if(learnset[i].level == level) return learn_step_move(&g, &learnset[i]);
    return NULL;
}

const Move* echo_next_move(const Echoid* e, uint8_t* at_level) {
    EchoGenes g;
    echo_genes(e->mac, &g);
    for(size_t i = 0; i < LEARNSET_LEN; i++) {
        if(learnset[i].level > e->level) {
            if(at_level) *at_level = learnset[i].level;
            return learn_step_move(&g, &learnset[i]);
        }
    }
    return NULL;
}

const char* move_type_short(const Move* m) {
    return m->type == MOVE_NEUTRAL ? "---" : echo_type_short(m->type);
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
    out->max_hp = grow(g.base_hp, e->level, 4);
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
/* Team / PAC roster                                                         */
/* ------------------------------------------------------------------------- */

static bool in_party(const Echoid* e) {
    return (e->flags & ECHO_FLAG_PARTY) != 0;
}

uint8_t roster_list(const Echoid* all, uint8_t count, bool party, uint8_t* out) {
    uint8_t n = 0;
    for(uint8_t i = 0; i < count; i++)
        if(in_party(&all[i]) == party) out[n++] = i;
    return n;
}

uint8_t roster_party_count(const Echoid* all, uint8_t count) {
    uint8_t n = 0;
    for(uint8_t i = 0; i < count; i++)
        if(in_party(&all[i])) n++;
    return n;
}

void roster_normalize(Echoid* all, uint8_t count, uint8_t* lead, bool fill) {
    if(count == 0) return;
    if(*lead >= count) *lead = 0;
    all[*lead].flags |= ECHO_FLAG_PARTY;
    uint8_t n = 1;
    /* Keep the first members already in the team, up to the limit. */
    for(uint8_t i = 0; i < count; i++) {
        if(i == *lead || !in_party(&all[i])) continue;
        if(n < PARTY_MAX)
            n++;
        else
            all[i].flags &= (uint8_t)~ECHO_FLAG_PARTY;
    }
    /* Fill free team slots from the PAC in order. */
    for(uint8_t i = 0; fill && i < count && n < PARTY_MAX; i++) {
        if(!in_party(&all[i])) {
            all[i].flags |= ECHO_FLAG_PARTY;
            n++;
        }
    }
}

bool team_remove(Echoid* team, uint8_t* count, uint8_t* lead, uint8_t idx, Echoid* out) {
    if(idx >= *count || *count <= 1) return false;
    if(out) *out = team[idx];
    for(uint8_t i = idx; i + 1 < *count; i++)
        team[i] = team[i + 1];
    (*count)--;
    if(*lead == idx)
        *lead = 0;
    else if(*lead > idx)
        (*lead)--;
    return true;
}

/* ------------------------------------------------------------------------- */
/* Crafting                                                                  */
/* ------------------------------------------------------------------------- */

const Recipe recipes[] = {
    {"Echo Ball", "3 Dull shards", ItemEchoBall, {3, 0, 0}, -1, 0, -1, 0, 0},
    {"Great Ball", "2 Bright + 1 Dull", ItemGreatBall, {1, 2, 0}, -1, 0, -1, 0, 0},
    {"Prism Ball", "1 Prismatic + 2 Bright", ItemPrismBall, {0, 2, 1}, -1, 0, -1, 0, 0},
    {"Tonic", "2 Tide + 1 Flora", ItemTonic, {0, 0, 0}, EchoTypeTide, 2, EchoTypeFlora, 1, 0},
    {"Super Tonic", "2 Tide 2 Flora 1 Bright", ItemSuperTonic, {0, 1, 0}, EchoTypeTide, 2, EchoTypeFlora, 2, 0},
    {"Mega Tonic", "3 Tide 3 Flora 2 Bright", ItemMegaTonic, {0, 2, 0}, EchoTypeTide, 3, EchoTypeFlora, 3, 0},
    {"Max Tonic", "3 Tide 3 Flora 1 Prism", ItemMaxTonic, {0, 0, 1}, EchoTypeTide, 3, EchoTypeFlora, 3, 0},
    {"Revive Crystal", "2 Pulse 1 Spark 1 Bright", ItemRevive, {0, 1, 0}, EchoTypePulse, 2, EchoTypeSpark, 1, 0},
    {"Echo Crystal", "5 shards of any kind", ItemCrystal, {0, 0, 0}, -1, 0, -1, 0, 5},
    {"PAC (+10 room)", "3 Dull + 2 Bright shards", CRAFT_OUT_PAC, {3, 2, 0}, -1, 0, -1, 0, 0},
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
    /* Element shards first (they use the cheapest tiers), then tier shards. */
    if(r->elem_a >= 0 && !take_elem(inv, (uint8_t)r->elem_a, r->elem_a_n)) return false;
    if(r->elem_b >= 0 && !take_elem(inv, (uint8_t)r->elem_b, r->elem_b_n)) return false;
    for(int k = SHARD_TIER_COUNT - 1; k >= 0; k--)
        if(!take_tier(inv, (uint8_t)k, r->tier_need[k])) return false;
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
    /* A PAC adds storage, which the caller tracks; everything else is a bag item. */
    uint8_t out = recipes[recipe].out;
    if(out < ItemCount && tmp.items[out] < 999) tmp.items[out]++;
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
    b->fought[0] = mine;
    b->fought_n = 1;
    echo_init(&b->wild, mac, level, ssid);
    b->result = BattleOngoing;
    char msg[BATTLE_LOG_LEN];
    snprintf(msg, sizeof(msg), "A wild %s appears!", echo_species_name(mac));
    battle_log(b, msg);
}

static void note_fought(Battle* b, Echoid* e) {
    for(uint8_t i = 0; i < b->fought_n; i++)
        if(b->fought[i] == e) return;
    if(b->fought_n < PARTY_MAX) b->fought[b->fought_n++] = e;
}

/* Performs one attack with a move. Returns true if the defender fainted. */
static bool do_attack(Battle* b, Echoid* atk, Echoid* def, const Move* mv, bool is_player) {
    EchoGenes gd;
    echo_genes(def->mac, &gd);
    EchoStats sa, sd;
    echo_stats(atk, &sa);
    echo_stats(def, &sd);

    const char* who = is_player ? "You" : "Foe";
    char msg[BATTLE_LOG_LEN];

    if(game_rand_range(100) >= mv->accuracy) {
        snprintf(msg, sizeof(msg), "%s %s missed", who, mv->name);
        battle_log(b, msg);
        return false;
    }

    uint32_t dmg = ((2u * atk->level / 5u + 2u) * mv->power * sa.atk / (sd.def ? sd.def : 1)) / 26u + 2u;
    uint8_t mult = mv->type == MOVE_NEUTRAL ? 100 : echo_type_mult(mv->type, gd.type);
    dmg = dmg * mult / 100u;
    dmg = dmg * (85u + game_rand_range(16)) / 100u;
    if(dmg < 1) dmg = 1;

    def->hp = dmg >= def->hp ? 0 : (uint16_t)(def->hp - dmg);

    const char* eff = mult > 100 ? " x2!" : (mult < 100 ? " x.5" : "");
    snprintf(msg, sizeof(msg), "%s %s -%lu%s", who, mv->name, (unsigned long)dmg, eff);
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
    /* Everyone who fought shares the XP equally. */
    uint32_t share = b->fought_n ? xp / b->fought_n : xp;
    if(share < 1) share = 1;
    b->level_before = b->mine->level;
    b->levels_gained = 0;
    for(uint8_t i = 0; i < b->fought_n; i++) {
        Echoid* e = b->fought[i];
        b->fought_level_before[i] = e->level;
        b->fought_xp[i] = share;
        uint8_t gained = echo_add_xp(e, share);
        if(e == b->mine) b->levels_gained = gained;
    }
}

/* Wild echoids mostly pick their best move against you, sometimes any move. */
static uint8_t enemy_pick_move(Battle* b, const Move* moves, uint8_t n) {
    if(n <= 1 || game_rand_range(100) < 30) return (uint8_t)game_rand_range(n);
    EchoGenes gm;
    echo_genes(b->mine->mac, &gm);
    uint8_t best = 0;
    uint32_t best_score = 0;
    for(uint8_t i = 0; i < n; i++) {
        uint32_t mult = moves[i].type == MOVE_NEUTRAL ? 100 : echo_type_mult(moves[i].type, gm.type);
        uint32_t score = (uint32_t)moves[i].power * moves[i].accuracy * mult;
        if(score > best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

static void enemy_turn(Battle* b) {
    if(b->result != BattleOngoing) return;
    Move moves[MAX_MOVES];
    uint8_t n = echo_moves(&b->wild, moves);
    if(n == 0) return;
    if(do_attack(b, &b->wild, b->mine, &moves[enemy_pick_move(b, moves, n)], false)) {
        b->result = BattleLost;
        battle_log(b, "Your echoid fainted!");
    }
}

static void player_attack(Battle* b, const Move* mv) {
    if(b->result != BattleOngoing) return;
    if(do_attack(b, b->mine, &b->wild, mv, true)) {
        b->result = BattleWon;
        award_xp(b, false);
        battle_log(b, "The wild echoid fled!");
    }
}

void battle_switch(Battle* b, Echoid* next, bool costs_turn) {
    if(b->result != BattleOngoing || !next || next == b->mine) return;
    b->mine = next;
    note_fought(b, next);
    char msg[BATTLE_LOG_LEN];
    snprintf(msg, sizeof(msg), "Go, %s!", echo_species_name(next->mac));
    battle_log(b, msg);
    if(costs_turn) enemy_turn(b);
}

void battle_attack(Battle* b, uint8_t slot) {
    if(b->result != BattleOngoing) return;
    Move moves[MAX_MOVES];
    uint8_t n = echo_moves(b->mine, moves);
    if(n == 0) return;
    if(slot >= n) slot = 0;
    Move mv = moves[slot];
    EchoStats sm, sw;
    echo_stats(b->mine, &sm);
    echo_stats(&b->wild, &sw);
    if(sm.spd >= sw.spd) {
        player_attack(b, &mv);
        enemy_turn(b);
    } else {
        enemy_turn(b);
        player_attack(b, &mv);
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
