/*
 * Generates docs/wiki/Types-and-Moves.md from the game's own tables, so the
 * wiki always matches the code.
 *
 *   cc -std=c11 -Isrc src/game.c tools/gen_wiki.c -o gen_wiki && ./gen_wiki > docs/wiki/Types-and-Moves.md
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

int main(void) {
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
    return 0;
}
