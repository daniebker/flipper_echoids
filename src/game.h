/*
 * Echoids - core game logic.
 *
 * This module is deliberately free of any Flipper SDK dependency so that the
 * deterministic generation, crafting and battle rules can be unit tested on a
 * desktop machine (see tests/test_game.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ECHO_TYPE_COUNT  8
#define SHARD_TIER_COUNT 3
#define MAX_TEAM         40
#define MAX_LEVEL        50
#define NAME_LEN         18
#define SPRITE_SIZE      16

/* Type cycle: each type is strong against the next one in the list. */
typedef enum {
    EchoTypeTide,
    EchoTypeEmber,
    EchoTypeFlora,
    EchoTypeStone,
    EchoTypeSpark,
    EchoTypeGale,
    EchoTypePulse,
    EchoTypeVoid,
} EchoType;

typedef enum {
    RarityCommon,
    RarityUncommon,
    RarityRare,
    RarityEpic,
    RarityLegendary,
    RarityCount,
} Rarity;

typedef enum {
    ShardDull,
    ShardBright,
    ShardPrismatic,
} ShardTier;

typedef enum {
    ItemEchoBall,
    ItemGreatBall,
    ItemPrismBall,
    ItemTonic,
    ItemCrystal,
    ItemCount,
} ItemId;

/* Everything that is fixed for a given MAC address. */
typedef struct {
    uint32_t seed;
    uint8_t type;
    uint8_t species;
    uint8_t rarity;
    uint8_t base_hp;
    uint8_t base_atk;
    uint8_t base_def;
    uint8_t base_spd;
} EchoGenes;

/* A creature instance (this is what gets saved). */
typedef struct {
    uint8_t mac[6];
    uint8_t level;
    uint8_t flags;
    uint16_t xp;
    uint16_t hp;
    char origin[NAME_LEN];
} Echoid;

typedef struct {
    uint16_t max_hp;
    uint16_t atk;
    uint16_t def;
    uint16_t spd;
} EchoStats;

typedef struct {
    uint8_t type;
    uint8_t tier;
} Shard;

typedef struct {
    uint16_t shards[ECHO_TYPE_COUNT][SHARD_TIER_COUNT];
    uint16_t items[ItemCount];
} Inventory;

/* ---------- RNG (battle randomness only, never used for generation) ---------- */
void game_rng_seed(uint32_t seed);
uint32_t game_rand(void);
uint32_t game_rand_range(uint32_t n); /* 0..n-1 */

/* ---------- Deterministic generation ---------- */
uint32_t echo_hash_mac(const uint8_t mac[6]);
void echo_genes(const uint8_t mac[6], EchoGenes* out);
const char* echo_species_name(const uint8_t mac[6]);
const char* echo_type_name(uint8_t type);
const char* echo_type_short(uint8_t type);
const char* echo_rarity_name(uint8_t rarity);
const char* echo_skill_name(uint8_t type);
const char* shard_tier_name(uint8_t tier);
const char* item_name(uint8_t item);
Shard shard_from_mac(const uint8_t mac[6]);
/* 16x16 sprite, bit 15 of rows[0] is the top-left pixel. */
void echo_sprite(const uint8_t mac[6], uint16_t rows[SPRITE_SIZE]);

/* ---------- Creature progression ---------- */
void echo_init(Echoid* e, const uint8_t mac[6], uint8_t level, const char* origin);
void echo_stats(const Echoid* e, EchoStats* out);
uint16_t echo_xp_to_next(uint8_t level);
/* Adds XP, returns the number of levels gained. */
uint8_t echo_add_xp(Echoid* e, uint32_t xp);
/* Wild creatures keep their MAC-derived genes but scale near the player's lead. */
uint8_t echo_wild_level(const uint8_t mac[6], uint8_t lead_level);
/* Damage multiplier in percent: 200 strong, 100 neutral, 50 resisted. */
uint8_t echo_type_mult(uint8_t attacker, uint8_t defender);
uint8_t echo_capture_chance(const Echoid* wild, uint8_t ball);

/* ---------- Crafting ---------- */
typedef struct {
    const char* name;
    const char* recipe;
    uint8_t out;
    uint8_t tier_need[SHARD_TIER_COUNT]; /* shards of a given tier, any element */
    int8_t elem_a; /* -1 = unused */
    uint8_t elem_a_n; /* shards of elem_a, any tier */
    int8_t elem_b;
    uint8_t elem_b_n;
    uint8_t any_n; /* shards of any element and tier */
} Recipe;

extern const Recipe recipes[];
extern const size_t recipe_count;

uint32_t inventory_shard_total(const Inventory* inv);

/* ---------- Shard cooldowns ---------- */
/* Compact duration: "<1m", "42m", "3h05m". */
void format_duration(uint32_t seconds, char* out, size_t size);

/* Seconds before the same Bluetooth device can drop its shard again. */
uint32_t shard_cooldown_s(uint8_t tier);

/* Cooldown table: parallel arrays of device hashes and the time (seconds,
 * RTC) their shard was last collected. Returns true if the shard can be
 * collected now (and records it). Otherwise returns false and, if
 * remaining_s is given, stores the seconds left. */
bool shard_claim(
    uint32_t* hashes,
    uint32_t* times,
    uint16_t* count,
    uint16_t max,
    uint32_t hash,
    uint8_t tier,
    uint32_t now,
    uint32_t* remaining_s);
bool craft_can(const Inventory* inv, size_t recipe);
bool craft_do(Inventory* inv, size_t recipe);

/* ---------- Battle ---------- */
typedef enum {
    BattleOngoing,
    BattleWon,
    BattleLost,
    BattleCaptured,
    BattleFled,
} BattleResult;

#define BATTLE_LOG_LINES 2
#define BATTLE_LOG_LEN   40

typedef struct {
    Echoid* mine;
    Echoid wild;
    uint8_t result;
    uint8_t levels_gained;
    uint32_t xp_gained;
    char log[BATTLE_LOG_LINES][BATTLE_LOG_LEN];
} Battle;

void battle_start(Battle* b, Echoid* mine, const uint8_t mac[6], uint8_t level, const char* ssid);
void battle_attack(Battle* b, bool skill);
/* Returns false if the player has no ball of that kind. */
bool battle_throw(Battle* b, Inventory* inv, uint8_t ball);
void battle_flee(Battle* b);
