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
#define PARTY_MAX        6 /* echoids carried in the team */
#define PAC_SIZE         10 /* echoids stored per crafted PAC (no limit on PACs) */
#define ECHO_FLAG_PARTY  0x01 /* Echoid.flags: in the team (else in the PAC) */
#define MAX_LEVEL        100
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
    ItemTonic, /* +20 HP */
    ItemCrystal,
    /* Added later: appended so saved item counts keep their meaning. */
    ItemSuperTonic, /* +50 HP */
    ItemMegaTonic, /* +100 HP */
    ItemMaxTonic, /* full HP */
    ItemRevive, /* brings a fainted echoid back with 1 HP */
    ItemCount,
} ItemId;

/* Saved item slots: room for new items without changing the save format. */
#define ITEM_SLOTS 16

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
    uint16_t items[ITEM_SLOTS];
} Inventory;

/* HP a healing item restores: 0 for non-tonics, ITEM_HEAL_FULL for a Max Tonic. */
#define ITEM_HEAL_FULL 0xFFFF
uint16_t item_heal_amount(uint8_t item);

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
/* ---------- Moves ---------- */
#define MAX_MOVES    4 /* moves an echoid can use at once */
#define MOVE_NEUTRAL ECHO_TYPE_COUNT /* Move.type for untyped moves */

typedef struct {
    const char* name;
    uint8_t type; /* EchoType, or MOVE_NEUTRAL (no type matchups) */
    uint8_t power;
    uint8_t accuracy; /* percent */
} Move;

/* The moves an echoid knows at its level: the MAX_MOVES most recently
 * learned, oldest first (later moves are stronger). Returns the count. */
uint8_t echo_moves(const Echoid* e, Move out[MAX_MOVES]);
/* The move learned on reaching exactly this level, or NULL. Level 1 moves
 * are known from the start and not reported. */
const Move* echo_move_learned_at(const uint8_t mac[6], uint8_t level);
/* The next move this echoid will learn, and at which level (NULL if none). */
const Move* echo_next_move(const Echoid* e, uint8_t* at_level);
const char* move_type_short(const Move* m);

#define TYPED_MOVES       6 /* moves per type */
#define SPECIES_PER_TYPE  3
/* False for opposed elements: the type an echoid beats and the type that beats it. */
bool move_type_learnable(uint8_t echo_type, uint8_t move_type);
/* Tables, for docs and tools. */
const Move* move_get(uint8_t type, uint8_t idx);
uint8_t learnset_len(void);
const Move* learnset_move(uint8_t type, uint8_t species, uint8_t i, uint8_t* level);
const char* species_name_of(uint8_t type, uint8_t species);
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

/* ---------- Team ---------- */
/* Removes team[idx] into *out, keeping order. Refuses the last member. The
 * lead index follows its echoid; if the lead itself leaves, the first
 * remaining member becomes lead. */
bool team_remove(Echoid* team, uint8_t* count, uint8_t* lead, uint8_t idx, Echoid* out);

/* ---------- Legacy roster (save versions 4-5 kept team + PAC in one array) ---------- */
/* Lists the array indexes of team (party=true) or PAC members, in order. */
uint8_t roster_list(const Echoid* all, uint8_t count, bool party, uint8_t* out);
uint8_t roster_party_count(const Echoid* all, uint8_t count);
/* Repairs flags: lead in the team and at most PARTY_MAX in the team. With
 * fill, free team slots are filled from the PAC in order (old saves). */
void roster_normalize(Echoid* all, uint8_t count, uint8_t* lead, bool fill);

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

/* Recipe.out value for a PAC: it adds storage instead of a bag item. */
#define CRAFT_OUT_PAC 0xFF

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
    uint8_t level_before; /* active echoid's level before XP was awarded */
    uint8_t levels_gained; /* ... and how many it gained */
    uint32_t xp_gained; /* total XP won; shared evenly by everyone who fought */

    /* Every echoid that fought in this battle (fainted ones included). */
    Echoid* fought[PARTY_MAX];
    uint8_t fought_n;
    uint32_t fought_xp[PARTY_MAX];
    uint8_t fought_level_before[PARTY_MAX];
    char log[BATTLE_LOG_LINES][BATTLE_LOG_LEN];
} Battle;

void battle_start(Battle* b, Echoid* mine, const uint8_t mac[6], uint8_t level, const char* ssid);
/* Player uses the move in slot (0..echo_moves()-1); the wild echoid answers. */
void battle_attack(Battle* b, uint8_t slot);
/* Sends in another echoid. A chosen switch costs the turn (the wild echoid
 * attacks the newcomer); replacing a fainted echoid does not. */
void battle_switch(Battle* b, Echoid* next, bool costs_turn);
/* Returns false if the player has no ball of that kind. */
bool battle_throw(Battle* b, Inventory* inv, uint8_t ball);
void battle_flee(Battle* b);
