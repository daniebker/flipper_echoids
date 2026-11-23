/*
 * Generates the wiki pages from the game's own tables, so they always match
 * the code:
 *
 *   cc -std=c11 -Isrc src/game.c tools/gen_wiki.c -o gen_wiki
 *   ./gen_wiki types > docs/wiki/Types.md
 *   ./gen_wiki moves > docs/wiki/Types-and-Moves.md
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
           "- [The type matchup chart](#the-type-matchup-chart)\n"
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

int main(int argc, char** argv) {
    if(argc > 1 && strcmp(argv[1], "types") == 0)
        gen_types();
    else
        gen_moves();
    return 0;
}
