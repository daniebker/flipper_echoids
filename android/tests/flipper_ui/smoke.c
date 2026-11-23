/*
 * Smoke test for the Android build's native layer, run on a PC: starts the
 * game on the Flipper shim, feeds it WiFi sightings, presses buttons through
 * every main screen, and checks it draws, saves and quits cleanly.
 * Set OUT=<dir> to also dump each screen as a .pbm image.
 *
 *   see .github/workflows/android.yml for the build command
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "card_reader_fake.h"
#include "gui/modules/text_input.h"
#include "scanner_phone.h"
#include "shim.h"

static uint8_t fb[64][128];
static int shots, failures;
static const char* out_dir;

static void* game(void* arg) {
    (void)arg;
    echoids_app(NULL);
    return NULL;
}

static void check_screen(const char* name) {
    usleep(150000);
    shim_render(fb);
    int black = 0;
    for(int y = 0; y < 64; y++)
        for(int x = 0; x < 128; x++)
            black += fb[y][x];
    if(black < 30) {
        printf("FAIL: screen '%s' is (nearly) blank\n", name);
        failures++;
    }
    if(out_dir) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%02d_%s.pbm", out_dir, shots, name);
        FILE* f = fopen(path, "w");
        if(f) {
            fprintf(f, "P1\n128 64\n");
            for(int y = 0; y < 64; y++) {
                for(int x = 0; x < 128; x++)
                    fputc(fb[y][x] ? '1' : '0', f);
                fputc('\n', f);
            }
            fclose(f);
        }
    }
    shots++;
}

/* The battle's bottom bar (rows 56-63) is solid black once it's over. */
static int battle_over(void) {
    usleep(150000);
    shim_render(fb);
    int black = 0;
    for(int x = 0; x < 128; x++)
        black += fb[60][x];
    return black > 110;
}

/* The dungeon screen has a full-width divider at row 52. */
static int on_dungeon_screen(void) {
    usleep(150000);
    shim_render(fb);
    int black = 0;
    for(int x = 0; x < 128; x++)
        black += fb[52][x];
    return black == 128;
}

static void press(InputKey k) {
    shim_input(k, InputTypeShort);
    usleep(120000);
}

static void wifi(int i) {
    ScanEvent e;
    memset(&e, 0, sizeof(e));
    e.type = ScanEventAp;
    uint8_t mac[6] = {0x10, 0x20, 0x30, 0x40, (uint8_t)(i >> 8), (uint8_t)i};
    memcpy(e.mac, mac, 6);
    e.rssi = (int8_t)(-50 - i % 40);
    snprintf(e.name, sizeof(e.name), "Net%d", i);
    scanner_phone_push(&e);
}

int main(void) {
    out_dir = getenv("OUT");
    char dir[] = "/tmp/echoids-smoke-XXXXXX";
    if(!mkdtemp(dir)) return 1;
    shim_set_data_dir(dir);
    uint8_t mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    shim_set_device(mac, "Phone");

    pthread_t t;
    pthread_create(&t, NULL, game, NULL);
    check_screen("intro");
    press(InputKeyOk);
    check_screen("menu");
    press(InputKeyOk);
    check_screen("collect");
    /* 300 access points: at any hour some of their echoids are out. */
    for(int i = 0; i < 300; i++)
        wifi(i);
    /* and some Bluetooth devices, for shards */
    for(int i = 0; i < 6; i++) {
        ScanEvent e;
        memset(&e, 0, sizeof(e));
        e.type = ScanEventBt;
        uint8_t m[6] = {0xB0, 0x11, 0x22, 0x33, 0x44, (uint8_t)i};
        memcpy(e.mac, m, 6);
        e.rssi = -60;
        snprintf(e.name, sizeof(e.name), "Buds%d", i);
        scanner_phone_push(&e);
    }
    usleep(1500000);
    check_screen("collect_list");
    press(InputKeyRight); /* the Shards page */
    check_screen("shards");
    press(InputKeyLeft); /* back to the echoids */
    press(InputKeyOk);
    check_screen("collect_popup");
    press(InputKeyOk); /* Battle! */
    check_screen("battle");
    press(InputKeyOk);
    check_screen("moves");
    press(InputKeyOk);
    check_screen("after_hit");
    press(InputKeyLeft); /* Run */
    /* Running away can fail (that's the game), so keep trying until the
     * battle ends: then the bottom bar is one solid "OK: continue" bar. */
    for(int tries = 0; tries < 30 && !battle_over(); tries++)
        press(InputKeyOk);
    if(!battle_over()) {
        printf("FAIL: couldn't leave the battle\n");
        failures++;
    }
    press(InputKeyOk); /* continue, back to Collect */

    /* Jack in to the network: password, connecting, then its dungeon. */
    shim_text_input_answer("secret");
    press(InputKeyOk);
    press(InputKeyDown); /* Jack in */
    press(InputKeyOk);
    check_screen("jacking_in");
    usleep(3200000); /* connecting, then the "Jacked in" popup fades */
    if(!on_dungeon_screen()) {
        printf("FAIL: didn't reach the dungeon\n");
        failures++;
    }
    check_screen("dungeon");
    press(InputKeyOk); /* the first room is always a fight */
    check_screen("dungeon_battle");
    for(int tries = 0; tries < 200 && !battle_over(); tries++)
        press(InputKeyOk); /* Hit, first move (or send in the next echoid) */
    press(InputKeyOk); /* continue */
    if(!on_dungeon_screen()) press(InputKeyOk); /* the results */
    if(on_dungeon_screen()) {
        check_screen("dungeon_after_fight");
        press(InputKeyBack);
        check_screen("dungeon_menu");
        press(InputKeyDown);
        press(InputKeyDown);
        press(InputKeyDown);
        press(InputKeyOk); /* Jack out: "Sure?" */
        press(InputKeyOk);
    } else {
        printf("(the team was ejected from the dungeon)\n");
    }
    check_screen("menu_again");

    /* Scan a card: its echoid comes out to battle. */
    usleep(2600000); /* let the "Jacked out" toast clear off the bottom bar */
    press(InputKeyDown);
    press(InputKeyOk);
    check_screen("card");
    static const uint8_t uid[] = {0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    card_reader_fake_present(CardNfc, uid, sizeof(uid));
    usleep(400000);
    check_screen("card_battle");
    press(InputKeyLeft); /* Run */
    for(int tries = 0; tries < 30 && !battle_over(); tries++)
        press(InputKeyOk);
    if(!battle_over()) {
        printf("FAIL: couldn't leave the card battle\n");
        failures++;
    }
    press(InputKeyOk); /* continue: back to the reader */
    if(!battle_over()) {
        /* The card is still on the reader: it mustn't start another battle. */
        usleep(400000);
        if(battle_over()) {
            printf("FAIL: the card on the reader started a second battle\n");
            failures++;
        }
    }
    check_screen("card_again");
    card_reader_fake_present(CardNfc, NULL, 0);
    press(InputKeyBack); /* back to the menu, on "Scan card" */
    press(InputKeyDown);
    press(InputKeyOk);
    check_screen("team");
    press(InputKeyOk);
    check_screen("details");
    press(InputKeyDown);
    check_screen("details_moves");
    press(InputKeyDown);
    check_screen("details_rating");
    press(InputKeyBack);
    press(InputKeyBack);
    press(InputKeyDown);
    press(InputKeyDown);
    press(InputKeyOk);
    check_screen("craft");
    press(InputKeyBack);
    press(InputKeyDown);
    press(InputKeyOk);
    check_screen("bag");
    press(InputKeyBack);
    press(InputKeyBack); /* quit from the main menu */
    usleep(500000);
    pthread_join(t, NULL);

    char save[600];
    snprintf(save, sizeof(save), "%s/save.bin", dir);
    struct stat st;
    if(stat(save, &st) != 0 || st.st_size == 0) {
        printf("FAIL: no save file written\n");
        failures++;
    }
    printf("%d screens checked, %d failure(s)\n", shots, failures);
    return failures ? 1 : 0;
}
