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
    usleep(1500000);
    check_screen("collect_list");
    press(InputKeyOk);
    check_screen("battle");
    press(InputKeyOk);
    check_screen("moves");
    press(InputKeyOk);
    check_screen("after_hit");
    press(InputKeyLeft); /* Run */
    press(InputKeyOk);
    press(InputKeyOk);
    press(InputKeyOk);
    press(InputKeyBack);
    check_screen("menu_again");
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
