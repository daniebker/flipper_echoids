/*
 * Generates the wiki pages from the game's own tables, so they always match
 * the code:
 *
 *   cc -std=c11 -Isrc src/game.c tools/gen_wiki.c -o gen_wiki
 *   ./gen_wiki types > docs/wiki/Types.md
 *   ./gen_wiki moves > docs/wiki/Types-and-Moves.md
 *   ./gen_wiki echoids > docs/wiki/Echoids.md
 *   ./gen_wiki items > docs/wiki/Items-and-Crafting.md
 *   ./gen_wiki battles > docs/wiki/Battles.md
 */
#include "game.h"

#include <stdio.h>
#include <string.h>

static const char* mult_text(uint8_t m) {
    return m > 100 ? "**2×**" : (m < 100 ? "½×" : "1×");
}

static const char* anchor(uint8_t t) {
    static char buf[16];
    snprintf(buf, sizeof(buf), "%s", echo_type_name(t));
    for(char* p = buf; *p; p++)
        if(*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
    return buf;
}

static uint8_t beats(uint8_t t) {
    return (uint8_t)((t + 1) % ECHO_TYPE_COUNT);
}

static uint8_t beaten_by(uint8_t t) {
    return (uint8_t)((t + ECHO_TYPE_COUNT - 1) % ECHO_TYPE_COUNT);
}

/* Flavour text, indexed by EchoType. */
static const char* const type_about[ECHO_TYPE_COUNT] = {
    "Water and currents. Tide echoids are often found on networks near the sea... or the kitchen sink.",
    "Fire and heat. Fast-burning and aggressive.",
    "Plants, roots and growth. Patient, and tougher than they look.",
    "Earth, rock and metal. Slow to move and hard to move.",
    "Electricity and lightning. The native element of every access point.",
    "Wind and air. Quick, light and hard to pin down.",
    "Sound, signals and radio waves. The echo in \"echoid\".",
    "Shadow and emptiness. What's left between the signals.",
};

/* Why each type beats the next one in the cycle, indexed by the winner. */
static const char* const type_why[ECHO_TYPE_COUNT] = {
    "water douses fire",
    "fire burns plants",
    "roots crack rock",
    "rock and earth ground electricity",
    "lightning splits the wind",
    "wind scatters and drowns out sound",
    "a signal cuts through the emptiness",
    "the abyss swallows the sea",
};

static void gen_types(void) {
    printf("# Types\n\n");
    printf("> Generated from the game code by `tools/gen_wiki.c`. Don't edit by hand: run the tool "
           "again after changing types.\n\n");
    printf("Every echoid has one of **%d types**, and so does every move. Types decide how much damage "
           "a move does: a move of the right type does double damage, and the wrong type does half.\n\n",
           ECHO_TYPE_COUNT);
    printf("See also: [Types and Moves](Types-and-Moves.md) for every move and learnset.\n\n");
    printf("## Contents\n\n- [The type cycle](#the-type-cycle)\n- [The types](#the-types)\n"
           "- [Names](#names)\n- [The type matchup chart](#the-type-matchup-chart)\n"
           "- [Strengths and weaknesses by type](#strengths-and-weaknesses-by-type)\n"
           "- [Quick reference: what to use against each type](#quick-reference-what-to-use-against-each-type)\n"
           "- [Types and learning moves](#types-and-learning-moves)\n- [Battle tips](#battle-tips)\n"
           "- [Where an echoid's type comes from](#where-an-echoids-type-comes-from)\n\n");

    /* Cycle */
    printf("## The type cycle\n\nThe types form one loop. **Each type beats the next one**, and is "
           "beaten by the one before it:\n\n");
    printf("```mermaid\nflowchart LR\n");
    for(uint8_t t = 0; t < ECHO_TYPE_COUNT; t++)
        printf("    %s -->|beats| %s\n", echo_type_name(t), echo_type_name(beats(t)));
    printf("```\n\n");
    printf("In text: **");
    for(uint8_t t = 0; t < ECHO_TYPE_COUNT; t++)
        printf("%s → ", echo_type_name(t));
    printf("%s**\n\n", echo_type_name(0));
    printf("The reasoning behind each link:\n\n");
    for(uint8_t t = 0; t < ECHO_TYPE_COUNT; t++)
        printf("- **%s beats %s**: %s.\n", echo_type_name(t), echo_type_name(beats(t)), type_why[t]);
    printf("\nBecause it's a single loop, every type has exactly **one type it beats**, **one type it's "
           "weak to**, and is neutral against the other %d.\n\n", ECHO_TYPE_COUNT - 3);

    /* Types */
    printf("## The types\n\n");
    printf("| Type | Short name | About | Species |\n|---|---|---|---|\n");
    for(uint8_t t = 0; t < ECHO_TYPE_COUNT; t++)
        printf("| **%s** | `%s` | %s | %s, %s, %s |\n", echo_type_name(t), echo_type_short(t), type_about[t],
               species_name_of(t, 0), species_name_of(t, 1), species_name_of(t, 2));
    printf("\nThe short name is what the Flipper's screens show (for example in the Collect list and the "
           "move picker). Each type has three species; rarer echoids are more likely to be the later, "
           "stronger-looking species.\n\n");

    /* Names */
    printf("## Names\n\n");
    printf("Besides its species, every echoid has **its own name**, generated from its MAC address "
           "by a Markov chain. The chain builds the name a letter at a time, choosing each letter "
           "from what follows the previous two letters in a list of words themed after the echoid's "
           "type. Names are %d to %d letters long and never just copy a word from the list. The same "
           "MAC always gives the same name.\n\n", ECHO_NAME_MIN, ECHO_NAME_MAX);
    printf("| Type | Source words (sample) | Example names |\n|---|---|---|\n");
    for(uint8_t t = 0; t < ECHO_TYPE_COUNT; t++) {
        printf("| **%s** | ", echo_type_name(t));
        const char* const* corpus = echo_name_corpus(t);
        for(int i = 0; i < 6 && corpus[i]; i++)
            printf("%s%s", i ? ", " : "", corpus[i]);
        printf(", … | ");
        int shown = 0;
        for(uint32_t i = 0; shown < 6; i++) {
            uint8_t mac[6] = {0x4E, 0x41, (uint8_t)(i >> 16), (uint8_t)(i >> 8), (uint8_t)i, 0x4D};
            EchoGenes g;
            echo_genes(mac, &g);
            if(g.type != t) continue;
            char name[ECHO_NAME_LEN];
            echo_name(mac, name, sizeof(name));
            printf("%s%s", shown ? ", " : "", name);
            shown++;
        }
        printf(" |\n");
    }
    printf("\n");

    /* Chart */
    printf("## The type matchup chart\n\n");
    printf("Find the **move's type** on the left and the **defending echoid's type** along the top. "
           "The cell is the damage multiplier.\n\n");
    printf("| Move ↓ / Defender → |");
    for(uint8_t d = 0; d < ECHO_TYPE_COUNT; d++)
        printf(" %s |", echo_type_short(d));
    printf("\n|---|");
    for(uint8_t d = 0; d < ECHO_TYPE_COUNT; d++)
        printf(":---:|");
    printf("\n");
    for(uint8_t a = 0; a < ECHO_TYPE_COUNT; a++) {
        printf("| **%s** |", echo_type_name(a));
        for(uint8_t d = 0; d < ECHO_TYPE_COUNT; d++) {
            uint8_t m = echo_type_mult(a, d);
            printf(" %s |", m > 100 ? "**2×**" : (m < 100 ? "½" : "·"));
        }
        printf("\n");
    }
    printf("\n**2×** = super effective (double damage) · **½** = not very effective (half damage) · "
           "**·** = normal damage\n\n");
    printf("Things to know:\n\n");
    printf("- **Only the move's type and the defender's type matter.** The attacker's own type doesn't "
           "change damage, so a Tide echoid using a Pulse move hits exactly like a Pulse echoid would.\n");
    printf("- **A type is neutral against itself.** Tide moves do normal damage to Tide echoids.\n");
    printf("- **Every move has a type**, so every attack in the game uses this chart.\n");
    printf("- In battle, the move picker shows `x2` or `x.5` next to a move when it's super effective "
           "or not very effective against the echoid you're fighting, and the battle log adds `x2!` or "
           "`x.5` to the damage.\n\n");

    /* Per type */
    printf("## Strengths and weaknesses by type\n\n");
    printf("| Type | Its moves do 2× to | Its moves do ½ to | Takes 2× from | Takes ½ from | Neutral (both ways) |\n");
    printf("|---|---|---|---|---|---|\n");
    for(uint8_t t = 0; t < ECHO_TYPE_COUNT; t++) {
        char neutral[128] = "";
        for(uint8_t o = 0; o < ECHO_TYPE_COUNT; o++) {
            if(o == t || o == beats(t) || o == beaten_by(t)) continue;
            if(neutral[0]) strcat(neutral, ", ");
            strcat(neutral, echo_type_name(o));
        }
        printf("| **%s** | %s | %s | %s | %s | %s, %s (itself) |\n", echo_type_name(t),
               echo_type_name(beats(t)), echo_type_name(beaten_by(t)), echo_type_name(beaten_by(t)),
               echo_type_name(beats(t)), neutral, echo_type_name(t));
    }
    printf("\nNotice the symmetry: a type's moves are weak against the very type that beats it, and it "
           "shrugs off moves from the type it beats. For example Tide moves do only half damage to Void "
           "(which beats Tide), and Ember moves do only half damage to Tide.\n\n");

    /* Quick reference */
    printf("## Quick reference: what to use against each type\n\n");
    printf("| Facing a… | Attack with… | Avoid attacking with… | Its moves will hurt… |\n|---|---|---|---|\n");
    for(uint8_t d = 0; d < ECHO_TYPE_COUNT; d++)
        printf("| **%s** | %s moves (2×) | %s moves (½) | %s echoids (2×) |\n", echo_type_name(d),
               echo_type_name(beaten_by(d)), echo_type_name(beats(d)), echo_type_name(beats(d)));
    printf("\n");

    /* Learning */
    printf("## Types and learning moves\n\n");
    printf("An echoid can **never** learn moves of its two *opposed* types: the type it beats and "
           "the type that beats it. Everything else is possible, but each species only learns "
           "the moves in its own learnset.\n\n");
    printf("| Type | Can never learn |\n|---|---|\n");
    for(uint8_t t = 0; t < ECHO_TYPE_COUNT; t++)
        printf("| %s | %s or %s moves |\n", echo_type_name(t), echo_type_name(beats(t)),
               echo_type_name(beaten_by(t)));
    printf("\nMost of an echoid's moves are its own type. On top of that, every echoid learns "
           "**counter moves**, of the type that beats whatever beats it (for example Tide is beaten by "
           "Void, Void is beaten by Pulse, so Tide echoids learn Pulse moves). They also learn a "
           "**species move** whose type depends on the species. So your echoid usually has an answer "
           "to the type that threatens it. Full learnsets: [Types and Moves](Types-and-Moves.md).\n\n");

    /* Tips */
    printf("## Battle tips\n\n");
    printf("- **Check the Collect panel before you fight.** It shows the wild echoid's type, so you know "
           "which of your moves will be `x2`.\n");
    printf("- **Use your counter moves.** If a wild echoid's type beats yours, your counter move is "
           "probably super effective against it.\n");
    printf("- **Switch when you're outmatched.** Use **Swap** to send in a team member whose moves beat "
           "the wild echoid. Switching costs your turn, so the wild echoid gets an attack on the newcomer.\n");
    printf("- **Wild echoids play the chart too.** They usually pick their most damaging move against "
           "your echoid, so expect super effective hits if their type beats yours.\n");
    printf("- **Build a mixed team.** With up to %d echoids in your team, covering several types means "
           "you'll always have someone to switch to.\n\n", PARTY_MAX);

    /* Source */
    printf("## Where an echoid's type comes from\n\n");
    printf("An echoid's type, like its species, rarity and stats, is worked out from a MAC address, "
           "so it never changes. Wild echoids come from the MAC address (BSSID) of a WiFi access "
           "point, and your starter comes from your Flipper's own Bluetooth MAC. The same access "
           "point is always the same echoid, with the same type. All %d types are equally common.\n",
           ECHO_TYPE_COUNT);
}

static void gen_moves(void) {
    printf("# Types and Moves\n\n");
    printf("> Generated from the game code by `tools/gen_wiki.c`. Don't edit by hand: run the tool again "
           "after changing moves or types.\n\n");
    printf("## Contents\n\n- [Types](#types)\n- [Type matchups](#type-matchups)\n"
           "- [Which move types each type can learn](#which-move-types-each-type-can-learn)\n"
           "- [How echoids learn moves](#how-echoids-learn-moves)\n- [All moves](#all-moves)\n"
           "- [Species and their moves](#species-and-their-moves)\n\n");

    /* Types */
    printf("## Types\n\nThere are %d types. Each one beats the next type in this cycle:\n\n", ECHO_TYPE_COUNT);
    printf("**");
    for(uint8_t t = 0; t < ECHO_TYPE_COUNT; t++)
        printf("%s → ", echo_type_name(t));
    printf("%s**\n\n", echo_type_name(0));
    printf("| Type | Short | Beats | Beaten by | Species |\n|---|---|---|---|---|\n");
    for(uint8_t t = 0; t < ECHO_TYPE_COUNT; t++) {
        printf(
            "| %s | `%s` | %s | %s | %s, %s, %s |\n",
            echo_type_name(t),
            echo_type_short(t),
            echo_type_name((uint8_t)((t + 1) % ECHO_TYPE_COUNT)),
            echo_type_name((uint8_t)((t + ECHO_TYPE_COUNT - 1) % ECHO_TYPE_COUNT)),
            species_name_of(t, 0),
            species_name_of(t, 1),
            species_name_of(t, 2));
    }
    printf("\nAn echoid's type, species, rarity and base stats all come from the MAC address of the "
           "WiFi access point it was found on (or, for your starter, your Flipper's Bluetooth MAC). "
           "Rarer echoids are more likely to be the later species of their type.\n\n");

    /* Matchups */
    printf("## Type matchups\n\nDamage multiplier when a move of the row's type hits an echoid of the "
           "column's type. Every move has a type, so this applies to all attacks.\n\n");
    printf("| Move ↓ / Echoid → |");
    for(uint8_t d = 0; d < ECHO_TYPE_COUNT; d++)
        printf(" %s |", echo_type_name(d));
    printf("\n|---|");
    for(uint8_t d = 0; d < ECHO_TYPE_COUNT; d++)
        printf("---|");
    printf("\n");
    for(uint8_t a = 0; a < ECHO_TYPE_COUNT; a++) {
        printf("| **%s** |", echo_type_name(a));
        for(uint8_t d = 0; d < ECHO_TYPE_COUNT; d++)
            printf(" %s |", mult_text(echo_type_mult(a, d)));
        printf("\n");
    }

    /* Learnable */
    printf("\n## Which move types each type can learn\n\n");
    printf("Echoids can **never** learn moves of the two *opposed* types: the type they beat and the "
           "type that beats them. Of the other types, each echoid only learns the ones in its own "
           "learnset (see below).\n\n");
    printf("| Echoid type |");
    for(uint8_t m = 0; m < ECHO_TYPE_COUNT; m++)
        printf(" %s moves |", echo_type_short(m));
    printf("\n|---|");
    for(uint8_t m = 0; m < ECHO_TYPE_COUNT; m++)
        printf("---|");
    printf("\n");
    for(uint8_t t = 0; t < ECHO_TYPE_COUNT; t++) {
        printf("| **%s** |", echo_type_name(t));
        for(uint8_t m = 0; m < ECHO_TYPE_COUNT; m++) {
            bool used = false;
            for(uint8_t sp = 0; sp < SPECIES_PER_TYPE; sp++)
                for(uint8_t i = 0; i < learnset_len(); i++)
                    if(learnset_move(t, sp, i, NULL)->type == m) used = true;
            const char* cell = !move_type_learnable(t, m) ? "✗ never" : (used ? "✓ learns" : "· allowed");
            printf(" %s |", cell);
        }
        printf("\n");
    }
    printf("\n✓ learns = at least one species of this type learns moves of that type · "
           "allowed = permitted but not in any current learnset · ✗ never = opposed type\n\n");

    /* Learning schedule */
    printf("## How echoids learn moves\n\n");
    printf("Echoids learn moves at fixed levels (max level %d) and can use their **%d most recently "
           "learned** moves; when a new one is learned, the oldest is forgotten. Later moves are "
           "stronger. Most moves are of the echoid's own type, plus two kinds of coverage moves:\n\n",
           MAX_LEVEL, MAX_MOVES);
    printf("- **Counter moves**: the type that beats whatever beats this echoid (for example Tide is "
           "beaten by Void, and Void is beaten by Pulse, so Tide echoids learn Pulse moves).\n");
    printf("- **Species move**: a type that depends on the species, so two echoids of the same type "
           "can cover different weaknesses.\n\n");
    printf("| Level | Learns |\n|---|---|\n");
    {
        /* Describe the schedule using Tide's first species and say where each comes from. */
        for(uint8_t i = 0; i < learnset_len(); i++) {
            uint8_t lv;
            const Move* own = learnset_move(0, 0, i, &lv);
            const Move* other = learnset_move(0, 1, i, NULL);
            const Move* ember = learnset_move(1, 0, i, NULL);
            const char* kind = "Own-type move";
            if(own->type != 0) kind = (other->type != own->type) ? "Species move" : "Counter move";
            (void)ember;
            printf("| %u | %s (power %u, accuracy %u%%) |\n", lv, kind, own->power, own->accuracy);
        }
    }

    /* All moves */
    printf("\n## All moves\n\nEach type has %d moves. *Learned by* lists the types whose echoids can "
           "learn that move.\n\n", TYPED_MOVES);
    for(uint8_t t = 0; t < ECHO_TYPE_COUNT; t++) {
        printf("### %s moves\n\n| Move | Power | Accuracy | Learned by |\n|---|---|---|---|\n", echo_type_name(t));
        for(uint8_t k = 0; k < TYPED_MOVES; k++) {
            const Move* mv = move_get(t, k);
            char who[200] = "";
            for(uint8_t et = 0; et < ECHO_TYPE_COUNT; et++) {
                bool learns = false;
                for(uint8_t sp = 0; sp < SPECIES_PER_TYPE; sp++)
                    for(uint8_t i = 0; i < learnset_len(); i++)
                        if(learnset_move(et, sp, i, NULL) == mv) learns = true;
                if(!learns) continue;
                /* Name the species if not every species of that type learns it. */
                char part[80];
                uint8_t sp_n = 0;
                char names[60] = "";
                for(uint8_t sp = 0; sp < SPECIES_PER_TYPE; sp++) {
                    bool l = false;
                    for(uint8_t i = 0; i < learnset_len(); i++)
                        if(learnset_move(et, sp, i, NULL) == mv) l = true;
                    if(l) {
                        if(sp_n++) strcat(names, ", ");
                        strcat(names, species_name_of(et, sp));
                    }
                }
                if(sp_n == SPECIES_PER_TYPE)
                    snprintf(part, sizeof(part), "%s", echo_type_name(et));
                else
                    snprintf(part, sizeof(part), "%s (%s)", echo_type_name(et), names);
                if(who[0]) strcat(who, ", ");
                strcat(who, part);
            }
            printf("| %s | %u | %u%% | %s |\n", mv->name, mv->power, mv->accuracy, who[0] ? who : "—");
        }
        printf("\n");
    }

    /* Species */
    printf("## Species and their moves\n\nThe full learnset of every species. Moves of another type "
           "are marked with their type.\n\n");
    for(uint8_t t = 0; t < ECHO_TYPE_COUNT; t++) {
        printf("### %s\n\nCan't learn: %s or %s moves.\n\n", echo_type_name(t),
               echo_type_name((uint8_t)((t + 1) % ECHO_TYPE_COUNT)),
               echo_type_name((uint8_t)((t + ECHO_TYPE_COUNT - 1) % ECHO_TYPE_COUNT)));
        printf("| Level |");
        for(uint8_t sp = 0; sp < SPECIES_PER_TYPE; sp++)
            printf(" %s |", species_name_of(t, sp));
        printf("\n|---|");
        for(uint8_t sp = 0; sp < SPECIES_PER_TYPE; sp++)
            printf("---|");
        printf("\n");
        for(uint8_t i = 0; i < learnset_len(); i++) {
            uint8_t lv;
            learnset_move(t, 0, i, &lv);
            printf("| %u |", lv);
            for(uint8_t sp = 0; sp < SPECIES_PER_TYPE; sp++) {
                const Move* mv = learnset_move(t, sp, i, NULL);
                if(mv->type == t)
                    printf(" %s |", mv->name);
                else
                    printf(" %s *(%s)* |", mv->name, echo_type_name(mv->type));
            }
            printf("\n");
        }
        printf("\n");
        (void)anchor;
    }
}


/* ------------------------------------------------------------------------- */

static void generated_note(void) {
    printf("> Generated from the game code by `tools/gen_wiki.c`. Don't edit by hand: run the tool "
           "again after changing the game.\n\n");
}

/* Finds a MAC whose echoid has the given rarity (and optionally type). */
static bool find_mac(uint8_t rarity, int type, uint8_t mac[6]) {
    for(uint32_t i = 0; i < 2000000; i++) {
        uint8_t m[6] = {0x57, 0x49, (uint8_t)(i >> 16), (uint8_t)(i >> 8), (uint8_t)i, 0x4B};
        EchoGenes g;
        echo_genes(m, &g);
        if(g.rarity == rarity && (type < 0 || g.type == type)) {
            memcpy(mac, m, 6);
            return true;
        }
    }
    return false;
}

static void gen_echoids(void) {
    printf("# Echoids\n\n");
    generated_note();
    printf("Everything about an echoid, apart from its level and XP, is worked out from a **MAC "
           "address**, so the same MAC always gives exactly the same echoid. Wild echoids come from "
           "the MAC (BSSID) of a WiFi access point, and your starter comes from your Flipper's own "
           "Bluetooth MAC.\n\n");
    printf("See also: [Types](Types.md) · [Types and Moves](Types-and-Moves.md) · "
           "[Battles](Battles.md) · [Items and Crafting](Items-and-Crafting.md)\n\n");
    printf("## Contents\n\n- [What the MAC decides](#what-the-mac-decides)\n- [Rarity](#rarity)\n"
           "- [Species](#species)\n- [Stats](#stats)\n- [Star rating](#star-rating)\n"
           "- [Levels and XP](#levels-and-xp)\n- [Names](#names)\n"
           "- [Time of day](#time-of-day)\n- [Team and PAC](#team-and-pac)\n\n");

    printf("## What the MAC decides\n\n| Trait | Notes |\n|---|---|\n");
    printf("| Type | One of %d, all equally common ([Types](Types.md)) |\n", ECHO_TYPE_COUNT);
    printf("| Rarity | Common to Legendary, see below |\n");
    printf("| Species | One of %d per type; rarer echoids lean to the later species |\n", SPECIES_PER_TYPE);
    printf("| Base stats | HP, ATK, DEF and SPD |\n");
    printf("| Star rating | How good its random stat rolls are |\n");
    printf("| Name | Generated by a Markov chain ([Types → Names](Types.md#names)) |\n");
    printf("| Sprite | A 16×16 symmetric pixel creature |\n");
    printf("| Moves | Its learnset, from its type and species ([Types and Moves](Types-and-Moves.md)) |\n");
    printf("| Active hours | When it comes out each day, see [Time of day](#time-of-day) |\n\n");
    printf("A wild echoid's **level** is the one thing the MAC doesn't decide. It's set near your "
           "lead's level, from 2 below to 2 above, plus 1 for Rare and rarer, so fights stay fair.\n\n");

    printf("## Rarity\n\n| Rarity | Badge | Chance | Stat bonus (HP / others) | Base catch rate |\n"
           "|---|---|---|---|---|\n");
    for(uint8_t r = 0; r < RarityCount; r++)
        printf("| %s | `%c`%s | %u%% | +%u / +%u | %u%% |\n", echo_rarity_name(r), echo_rarity_letter(r),
               r >= RarityEpic ? " (boxed)" : "", rarity_chance_pct(r), r * 3, r * 2, capture_base_pct(r));
    printf("\nThe badge letter is shown in the Collect list, the Team and PAC lists and in battle. "
           "Epic and Legendary badges are boxed so they stand out. See [Battles](Battles.md#catching) "
           "for how the catch rate is used.\n\n");

    printf("## Species\n\nEach type has three species. An echoid is usually the first one, and rarer "
           "echoids are more likely to be the later, stronger-looking ones:\n\n");
    printf("| Species slot | Chance | Epic and Legendary |\n|---|---|---|\n");
    printf("| 1st | 60%% | moved up to the 2nd |\n| 2nd | 30%% | moved up to the 3rd |\n"
           "| 3rd | 10%% | stays 3rd |\n\n");
    printf("| Type | 1st | 2nd | 3rd |\n|---|---|---|---|\n");
    for(uint8_t t = 0; t < ECHO_TYPE_COUNT; t++)
        printf("| %s | %s | %s | %s |\n", echo_type_name(t), species_name_of(t, 0), species_name_of(t, 1),
               species_name_of(t, 2));
    printf("\n");

    printf("## Stats\n\nEvery echoid has four stats. Its **base** stats are a fixed floor, plus a "
           "**random roll** from its MAC, plus its rarity bonus:\n\n");
    printf("| Stat | Floor | Random roll | Rarity bonus |\n|---|---|---|---|\n");
    printf("| HP | %d | 0–%d | +3 per rarity step |\n", STAT_HP_FLOOR, STAT_HP_ROLL_MAX);
    for(uint8_t k = StatAtk; k < STAT_COUNT; k++)
        printf("| %s | %d | 0–%d | +2 per rarity step |\n", stat_name(k), STAT_FLOOR, STAT_ROLL_MAX);
    printf("\nStats grow with level: `stat = base + base × (level − 1) / 8 + growth × (level − 1)`, "
           "where growth is 4 for HP and 1 for the others. **ATK** raises your damage, **DEF** lowers "
           "the damage you take, **SPD** decides who moves first and how easily you run away, and "
           "**HP** is how much damage the echoid can take.\n\n");

    printf("## Star rating\n\nThe star rating judges only the **random rolls**, not rarity, so even "
           "a Common can be perfect.\n\n");
    printf("| Stat stars | Roll |\n|---|---|\n| ★★★ | The highest possible roll: the stat can't be "
           "stronger |\n| ★★☆ | The top half of the range |\n| ★☆☆ | The bottom half |\n\n");
    /* Exact odds: every combination of rolls is equally likely. */
    uint32_t overall[4] = {0}, combos = 0;
    for(uint8_t hp = 0; hp <= STAT_HP_ROLL_MAX; hp++)
        for(uint8_t a = 0; a <= STAT_ROLL_MAX; a++)
            for(uint8_t d = 0; d <= STAT_ROLL_MAX; d++)
                for(uint8_t sp = 0; sp <= STAT_ROLL_MAX; sp++) {
                    uint8_t total = stat_stars(hp, STAT_HP_ROLL_MAX) + stat_stars(a, STAT_ROLL_MAX) +
                                    stat_stars(d, STAT_ROLL_MAX) + stat_stars(sp, STAT_ROLL_MAX);
                    overall[total == STAT_COUNT * 3 ? 3 : (total >= 8 ? 2 : 1)]++;
                    combos++;
                }
    printf("The four stats' stars add up to between %d and %d, giving the overall rating:\n\n",
           STAT_COUNT, STAT_COUNT * 3);
    printf("| Overall | Star total | How common |\n|---|---|---|\n");
    printf("| ★★★ **perfect** | 12: every stat ★★★ | 1 in %lu |\n",
           (unsigned long)(overall[3] ? combos / overall[3] : 0));
    printf("| ★★☆ | 8–11 | %.1f%% |\n", 100.0 * overall[2] / combos);
    printf("| ★☆☆ | 4–7 | %.1f%% |\n\n", 100.0 * overall[1] / combos);
    printf("The overall stars appear in the Collect info panel and on the details screen. The "
           "details screen's **Rating** page (↑/↓) shows each roll, for example `ATK 7/8`.\n\n");

    printf("## Levels and XP\n\nEchoids level up to **%d**. XP needed for the next level is "
           "`10 + 4 × level²`:\n\n| Level | XP to next |\n|---|---|\n", MAX_LEVEL);
    uint8_t lv[] = {1, 5, 10, 25, 50, 75, 99};
    for(size_t i = 0; i < sizeof(lv); i++)
        printf("| %u | %u |\n", lv[i], echo_xp_to_next(lv[i]));
    printf("\nXP comes from winning and catching ([Battles](Battles.md#xp)) and from Echo Crystals "
           "([Items and Crafting](Items-and-Crafting.md)). Levelling up also restores the HP the "
           "echoid gained, and teaches new moves at set levels.\n\n");

    printf("## Names\n\nEvery echoid has its own name as well as its species, built by a Markov "
           "chain from words themed after its type. Names are %d–%d letters and always the same for "
           "the same MAC. Details and examples: [Types → Names](Types.md#names).\n\n",
           ECHO_NAME_MIN, ECHO_NAME_MAX);

    printf("## Time of day\n\nWild echoids only come out at **certain hours**, taken from the "
           "Flipper's clock. Each echoid has one or two daily windows, fixed by its MAC, so the same "
           "time of day gives the same encounters and a different hour gives others. Rarer echoids "
           "are out for less time:\n\n");
    printf("| Rarity | Hours per window | Average hours out per day |\n|---|---|---|\n");
    for(uint8_t r = 0; r < RarityCount; r++) {
        uint8_t lo, hi;
        active_window_len(r, &lo, &hi);
        uint32_t hours = 0, cnt = 0;
        for(uint32_t i = 0; i < 400000 && cnt < 3000; i++) {
            uint8_t m[6] = {0x54, 0x4F, (uint8_t)(i >> 16), (uint8_t)(i >> 8), (uint8_t)i, 0x44};
            EchoGenes g;
            echo_genes(m, &g);
            if(g.rarity != r) continue;
            uint32_t mask = echo_active_hours(m);
            for(int h = 0; h < 24; h++)
                hours += (mask >> h) & 1u;
            cnt++;
        }
        printf("| %s | %u–%u | %.1f |\n", echo_rarity_name(r), lo, hi, cnt ? (double)hours / cnt : 0.0);
    }
    printf("\nAccess points whose echoid is asleep don't appear in Collect. If nothing is out, "
           "Collect says how many echoids are asleep at the current hour. Bluetooth shards and the "
           "echoids you own aren't affected.\n\n");

    printf("## Team and PAC\n\n- Your **team** holds up to %d echoids. The **lead** starts each "
           "battle.\n", PARTY_MAX);
    printf("- The rest go to **Cold PAC Storage**, %d echoids per PAC. You start with one PAC and can "
           "craft as many more as you like ([Items and Crafting](Items-and-Crafting.md#pacs)).\n",
           PAC_SIZE);
    printf("- An echoid that comes out of the PAC has full HP.\n");
    printf("- You can't catch an echoid you already own. The Collect list marks those with `*`.\n");
}

static void gen_items(void) {
    printf("# Items and Crafting\n\n");
    generated_note();
    printf("Bluetooth devices drop **shards**, and shards are crafted into **items** in the "
           "**Craft** menu.\n\n");
    printf("See also: [Echoids](Echoids.md) · [Battles](Battles.md)\n\n");
    printf("## Contents\n\n- [Shards](#shards)\n- [Shard cooldowns](#shard-cooldowns)\n"
           "- [Recipes](#recipes)\n- [Healing and reviving](#healing-and-reviving)\n"
           "- [Echo Balls](#echo-balls)\n- [PACs](#pacs)\n- [How shards are spent](#how-shards-are-spent)\n\n");

    printf("## Shards\n\nEvery Bluetooth device's MAC address decides the **element** and **tier** "
           "of the shard it drops, so the same device always drops the same shard. Elements are the "
           "%d [types](Types.md), all equally common.\n\n| Tier | Chance |\n|---|---|\n", ECHO_TYPE_COUNT);
    for(uint8_t k = 0; k < SHARD_TIER_COUNT; k++)
        printf("| %s | %u%% |\n", shard_tier_name(k), shard_tier_chance_pct(k));
    printf("\nThe Bag's **Shards** tab lists your shards by element as Dull/Bright/Prismatic.\n\n");

    printf("## Shard cooldowns\n\nAfter a device drops its shard it has to **recharge** before it "
           "drops it again. The cooldown uses the Flipper's clock, so leaving Collect or the app "
           "doesn't reset it. Rarer shards take longer:\n\n| Tier | Cooldown |\n|---|---|\n");
    for(uint8_t k = 0; k < SHARD_TIER_COUNT; k++) {
        char d[16];
        format_duration(shard_cooldown_s(k), d, sizeof(d));
        printf("| %s | %s |\n", shard_tier_name(k), d);
    }
    printf("\nThe shortest cooldown is 3 hours, and each rarer tier takes ten times longer. "
           "Collect's **Shards** page (left/right on the Flipper, the Shards button on Android) "
           "lists every device you pass with its shard and the time left until it's ready "
           "again. Shards don't pop up.\n\n");

    printf("## Recipes\n\n| Item | Recipe | Effect |\n|---|---|---|\n");
    for(size_t i = 0; i < recipe_count; i++) {
        const Recipe* r = &recipes[i];
        const char* effect = "";
        char buf[96];
        uint16_t heal = r->out < ItemCount ? item_heal_amount(r->out) : 0;
        if(r->out == CRAFT_OUT_PAC) {
            snprintf(buf, sizeof(buf), "Room for %d more echoids in storage", PAC_SIZE);
            effect = buf;
        } else if(heal == ITEM_HEAL_FULL) {
            effect = "Fully restores HP (not on a fainted echoid)";
        } else if(heal) {
            snprintf(buf, sizeof(buf), "Restores %u HP (not on a fainted echoid)", heal);
            effect = buf;
        } else if(r->out == ItemRevive) {
            effect = "Brings a fainted echoid back with 1 HP";
        } else if(r->out == ItemCrystal) {
            effect = "Gives an echoid half the XP it needs for its next level";
        } else if(r->out <= ItemPrismBall) {
            snprintf(buf, sizeof(buf), "Catch rate ×%u.%u", ball_mult_x10(r->out) / 10, ball_mult_x10(r->out) % 10);
            effect = buf;
        }
        printf("| %s | %s | %s |\n", r->name, r->recipe, effect);
    }
    printf("\n**Prism** means a Prismatic shard. Element names (Tide, Flora…) accept that element's "
           "shards of any tier, and tier names (Dull, Bright…) accept that tier from any element.\n\n");

    printf("## Healing and reviving\n\n- Open the **Bag**, pick a Tonic, Revive Crystal or Echo "
           "Crystal and press **OK**, then choose the team member to use it on. PAC echoids can't "
           "be chosen.\n");
    printf("- **Tonics don't work on a fainted echoid.** Use a **Revive Crystal** first. It "
           "comes back with 1 HP.\n");
    printf("- Team members who haven't fainted regain 1 HP every 5 seconds outside battle. Fainted "
           "echoids don't regain HP until revived.\n");
    printf("- Echoids that come out of the PAC have full HP, fainted or not.\n\n");

    printf("## Echo Balls\n\nThrow balls from the battle menu's **Ball** option. The ball menu shows "
           "each ball's catch chance against the echoid you're fighting.\n\n| Ball | Catch rate |\n"
           "|---|---|\n");
    for(uint8_t b = 0; b <= ItemPrismBall; b++)
        printf("| %s | ×%u.%u |\n", item_name(b), ball_mult_x10(b) / 10, ball_mult_x10(b) % 10);
    printf("\nFull catch rules: [Battles → Catching](Battles.md#catching).\n\n");

    printf("## PACs\n\nEach PAC stores **%d** echoids in Cold PAC Storage, on top of your team of "
           "%d. You start with one PAC, and there's **no limit** on how many more you craft. Stored "
           "echoids live in `/ext/apps_data/echoids/pac.bin` on the SD card, %u bytes each.\n\n",
           PAC_SIZE, PARTY_MAX, (unsigned)sizeof(Echoid));

    printf("## How shards are spent\n\nRecipes that name an **element** use that element's "
           "cheapest shards first. After that, **tier** requirements take shards of that tier from "
           "whichever element you have the most of, and \"any\" shards take the cheapest. So a "
           "Bright shard isn't used up on a \"2 Tide\" requirement while a Dull Tide shard is "
           "available.\n");
}

static void gen_battles(void) {
    printf("# Battles\n\n");
    generated_note();
    printf("Pick a wild echoid in **Collect** and press **OK** to battle it.\n\n");
    printf("See also: [Types](Types.md) · [Types and Moves](Types-and-Moves.md) · [Echoids](Echoids.md)\n\n");
    printf("## Contents\n\n- [Starting a battle](#starting-a-battle)\n- [Your turn](#your-turn)\n"
           "- [Damage](#damage)\n- [Switching and fainting](#switching-and-fainting)\n"
           "- [Catching](#catching)\n- [Running away](#running-away)\n- [XP](#xp)\n\n");

    printf("## Starting a battle\n\n- Your **lead** fights first. If it has fainted, the first "
           "healthy team member starts instead. If the whole team has fainted, you can't battle.\n");
    printf("- The wild echoid's level is set near your lead's: from 2 below to 2 above, plus 1 for "
           "Rare and rarer.\n");
    printf("- You can't battle an echoid you already own, one you've already fought or fled from "
           "this visit, or one that has just gone to sleep ([time of day](Echoids.md#time-of-day)).\n\n");

    printf("## Your turn\n\nThe battle menu has four actions:\n\n| Action | What it does |\n|---|---|\n");
    printf("| **Hit** | Pick one of your echoid's moves (up to %d). Each row shows `x2`/`x.5` against "
           "this foe, the move's type and its power. |\n", MAX_MOVES);
    printf("| **Swap** | Send in another team member. Uses your turn. |\n");
    printf("| **Ball** | Throw an Echo Ball to try to catch the echoid. |\n");
    printf("| **Run** | Try to escape. |\n\n");
    printf("When you attack, the faster echoid (higher **SPD**) moves first; ties go to you. Wild "
           "echoids usually pick the move that does the most damage to your echoid, and sometimes "
           "a random one.\n\n");

    printf("## Damage\n\n```\ndamage = ((2 × level / 5 + 2) × power × ATK / DEF) / 26 + 2\n"
           "       × type multiplier (2, 1 or ½)\n       × random 85–100%%\n```\n\n");
    printf("- Every move has a type, and the [type chart](Types.md#the-type-matchup-chart) applies. "
           "The battle log adds `x2!` or `x.5` to the damage.\n");
    printf("- Each move has an **accuracy**. A miss does no damage and shows `missed` in the log.\n");
    printf("- An attack always does at least 1 damage.\n\n");

    printf("## Switching and fainting\n\n- **Swap** sends in any healthy team member, not one that "
           "has fainted. The wild echoid gets a free attack on the newcomer.\n");
    printf("- When your echoid **faints**, you choose who goes next, as long as anyone on your team "
           "can still fight. That switch is free. The battle is only lost when the whole team has "
           "fainted.\n");
    printf("- Fainted echoids stay fainted until a Revive Crystal ([Items and "
           "Crafting](Items-and-Crafting.md#healing-and-reviving)).\n\n");

    printf("## Catching\n\nCatch chance = base rate for its rarity × ball multiplier × "
           "`(3 × maxHP − 2 × HP) / (3 × maxHP)`, kept between 1%% and 95%%. A weakened echoid is "
           "easier to catch: at full HP the last part is ⅓, and near 0 HP it's almost 1.\n\n");
    printf("| Rarity | Ball | At full HP | At 1 HP |\n|---|---|---|---|\n");
    for(uint8_t r = 0; r < RarityCount; r++) {
        uint8_t mac[6];
        if(!find_mac(r, -1, mac)) continue;
        for(uint8_t b = 0; b <= ItemPrismBall; b++) {
            Echoid w;
            echo_init(&w, mac, 20, NULL);
            uint8_t full = echo_capture_chance(&w, b);
            w.hp = 1;
            uint8_t low = echo_capture_chance(&w, b);
            printf("| %s | %s | %u%% | %u%% |\n", b == 0 ? echo_rarity_name(r) : "", item_name(b), full, low);
        }
    }
    printf("\nA failed throw lets the wild echoid attack. A caught echoid joins your team, or goes to "
           "the PAC when the team is full. If there's no room anywhere, craft a PAC first.\n\n");

    printf("## Running away\n\nRun chance = `50%% + 5%% × (your SPD − its SPD)`, kept between 20%% and "
           "95%%. If it fails, the wild echoid attacks. An echoid you fled from can't be battled "
           "again this visit.\n\n");

    printf("## XP\n\nWinning gives `(8 + 5 × wild level) × (1 + 0.25 × rarity step)` XP, and "
           "catching gives 1.5× that:\n\n| Wild level |");
    for(uint8_t r = 0; r < RarityCount; r++)
        printf(" %s |", echo_rarity_name(r));
    printf(" Legendary caught |\n|---|");
    for(uint8_t r = 0; r <= RarityCount; r++)
        printf("---|");
    printf("\n");
    uint8_t lv[] = {1, 5, 10, 25, 50, 100};
    for(size_t i = 0; i < sizeof(lv); i++) {
        printf("| %u |", lv[i]);
        for(uint8_t r = 0; r < RarityCount; r++)
            printf(" %lu |", (unsigned long)battle_xp_reward(lv[i], r, false));
        printf(" %lu |\n", (unsigned long)battle_xp_reward(lv[i], RarityLegendary, true));
    }
    printf("\nThe XP is **shared evenly by every echoid that fought**: the starter and everyone you "
           "switched in, including any that fainted. Each gets at least 1. After the battle, a "
           "**Results** screen lists each echoid's XP, level-ups and new moves.\n");
}

int main(int argc, char** argv) {
    const char* page = argc > 1 ? argv[1] : "moves";
    if(strcmp(page, "types") == 0)
        gen_types();
    else if(strcmp(page, "echoids") == 0)
        gen_echoids();
    else if(strcmp(page, "items") == 0)
        gen_items();
    else if(strcmp(page, "battles") == 0)
        gen_battles();
    else
        gen_moves();
    return 0;
}
