/*
 * Echoids - a creature collecting game for the Flipper Zero.
 *
 * WiFi access points are wild echoids that can be battled and captured.
 * Bluetooth devices drop shards which are crafted into echo balls and items.
 * Everything is derived deterministically from MAC addresses.
 */
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <input/input.h>
#include <notification/notification_messages.h>

#include <stdio.h>
#include <string.h>

#include "game.h"
#include "save.h"
#include "scanner.h"

#define NEARBY_MAX       24
#define REGEN_MS         5000
#define AUTOSAVE_MS      20000
#define TOAST_MS         2500
#define BOARD_TIMEOUT_MS 6000
#define TEAM_ROWS        5
#define NEARBY_ROWS      3
#define CRAFT_ROWS       3
#define SPOTTED_MAX      128

typedef enum {
    ScreenIntro,
    ScreenMenu,
    ScreenCollect,
    ScreenBattle,
    ScreenBallPick,
    ScreenTeam,
    ScreenDetail,
    ScreenCraft,
    ScreenBag,
    ScreenSettings,
    ScreenHelp,
    ScreenBoard,
    ScreenTerminal,
    ScreenCalibrate,
    ScreenUseOn, /* pick which echoid a Tonic / Crystal is used on */
} Screen;

typedef enum {
    NearbyWild,
    NearbyOwned,
    NearbyDone, /* defeated or fled this session */
} NearbyState;

typedef struct {
    uint8_t mac[6];
    int8_t rssi;
    uint8_t state;
    char ssid[NAME_LEN];
} Nearby;

typedef struct {
    FuriMutex* mutex;
    FuriMessageQueue* queue;
    ViewPort* view_port;
    Gui* gui;
    NotificationApp* notif;
    Scanner* scanner;

    SaveData save;
    bool dirty;
    bool running;
    uint32_t last_save;
    uint32_t last_regen;
    uint32_t scan_started;

    Screen screen;
    uint8_t menu_idx;

    Nearby nearby[NEARBY_MAX];
    uint8_t nearby_count;
    uint8_t nearby_idx;
    uint8_t nearby_scroll;
    uint8_t nearby_next_replace;

    Battle battle;
    int8_t battle_nearby;
    uint8_t battle_menu;
    uint8_t ball_idx;

    uint8_t team_idx;
    uint8_t team_scroll;
    uint8_t craft_idx;
    uint8_t craft_scroll;
    uint8_t bag_tab;
    uint8_t bag_idx;
    uint8_t use_item_id;
    uint8_t use_idx;
    uint8_t use_scroll;
    uint8_t settings_idx;
    bool confirm_reset;
    uint8_t help_scroll;

    /* Board setup / debug */
    uint8_t board_idx;
    uint8_t board_scroll;
    uint8_t term_cmd;
    bool term_otg_by_us;
    uint8_t cal_step; /* 0 = warm-up, 1..N = candidate, N+1 = done */
    uint32_t cal_tick;
    bool cal_sent;
    bool cal_otg_by_us;
    int32_t cal_best_score;
    int16_t cal_best;
    char cal_result[32];

    /* Counters for this outing (since the app was opened). */
    uint32_t spotted[SPOTTED_MAX]; /* MAC hashes of echoids already counted */
    uint16_t spotted_count;
    uint16_t shards_this_run;

    char toast[40];
    uint32_t toast_until;
} App;

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static void app_toast(App* app, const char* msg) {
    strncpy(app->toast, msg, sizeof(app->toast) - 1);
    app->toast[sizeof(app->toast) - 1] = '\0';
    app->toast_until = furi_get_tick() + furi_ms_to_ticks(TOAST_MS);
}

static Echoid* app_lead(App* app) {
    return &app->save.team[app->save.lead];
}

static bool app_owns(App* app, const uint8_t mac[6]) {
    for(uint8_t i = 0; i < app->save.team_count; i++) {
        if(memcmp(app->save.team[i].mac, mac, 6) == 0) return true;
    }
    return false;
}

static void app_new_game(App* app) {
    /* Keep the board configuration when starting over. */
    SaveData old = app->save;
    bool keep = old.magic == SAVE_MAGIC;
    memset(&app->save, 0, sizeof(app->save));
    app->save.magic = SAVE_MAGIC;
    app->save.version = SAVE_VERSION;
    app->save.source = ScanSourceBoard;
    app->save.uart = ScanUartUsart;
    app->save.bt_enabled = 1;
    if(keep) {
        app->save.source = old.source;
        app->save.uart = old.uart;
        app->save.bt_enabled = old.bt_enabled;
        app->save.baud_idx = old.baud_idx;
        app->save.power = old.power;
        app->save.boot_idx = old.boot_idx;
        app->save.eol = old.eol;
        app->save.wifi_cmd = old.wifi_cmd;
        app->save.bt_cmd = old.bt_cmd;
        app->save.log_sd = old.log_sd;
    }

    /* The starter is born from this Flipper's own Bluetooth MAC. */
    const uint8_t* mac = furi_hal_version_get_ble_mac();
    const char* name = furi_hal_version_get_name_ptr();
    echo_init(&app->save.team[0], mac, 1, name ? name : "Flipper");
    app->save.team_count = 1;
    app->save.lead = 0;
    app->dirty = true;
}

static void app_scan_config(App* app, ScanConfig* cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->uart = app->save.uart % ScanUartCount;
    cfg->baud_idx = app->save.baud_idx % scanner_baud_count;
    cfg->power = app->save.power % ScanPowerCount;
    cfg->boot_idx = app->save.boot_idx % scanner_boot_count;
    cfg->eol = app->save.eol & 1;
    cfg->wifi_cmd = app->save.wifi_cmd % scanner_wifi_cmd_count;
    cfg->bt_cmd = app->save.bt_cmd % scanner_bt_cmd_count;
    cfg->bt_enabled = app->save.bt_enabled;
    cfg->log_sd = app->save.log_sd;
}

static void app_save(App* app) {
    if(save_store(&app->save)) {
        app->dirty = false;
    }
    app->last_save = furi_get_tick();
}

static const char* nearby_state_mark(uint8_t state) {
    switch(state) {
    case NearbyOwned:
        return "*";
    case NearbyDone:
        return "x";
    default:
        return " ";
    }
}

/* ------------------------------------------------------------------------- */
/* Drawing primitives                                                        */
/* ------------------------------------------------------------------------- */

static void draw_sprite(Canvas* canvas, const uint8_t mac[6], int x, int y, int scale) {
    uint16_t rows[SPRITE_SIZE];
    echo_sprite(mac, rows);
    for(int py = 0; py < SPRITE_SIZE; py++) {
        for(int px = 0; px < SPRITE_SIZE; px++) {
            if(rows[py] & (1u << (15 - px))) {
                canvas_draw_box(canvas, x + px * scale, y + py * scale, scale, scale);
            }
        }
    }
}

static void draw_hp_bar(Canvas* canvas, int x, int y, int w, uint16_t hp, uint16_t max) {
    canvas_draw_frame(canvas, x, y, w, 5);
    int fill = max ? (int)((uint32_t)(w - 2) * hp / max) : 0;
    if(fill > 0) canvas_draw_box(canvas, x + 1, y + 1, fill, 3);
}

static void draw_title(Canvas* canvas, const char* title, const char* right) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, title);
    if(right) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignBottom, right);
    }
    canvas_draw_line(canvas, 0, 12, 127, 12);
    canvas_set_font(canvas, FontSecondary);
}

static void draw_row(Canvas* canvas, int y, bool selected) {
    /* y is the text baseline */
    if(selected) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, y - 8, 123, 10);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_set_color(canvas, ColorBlack);
    }
}

static void draw_toast(App* app, Canvas* canvas) {
    if(!app->toast[0] || furi_get_tick() > app->toast_until) return;
    canvas_set_font(canvas, FontSecondary);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 0, 51, 128, 13);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, 0, 51, 128, 13, 2);
    canvas_draw_str_aligned(canvas, 64, 58, AlignCenter, AlignCenter, app->toast);
}

/* ------------------------------------------------------------------------- */
/* Screens: drawing                                                          */
/* ------------------------------------------------------------------------- */

static const char* const menu_items[] = {"Collect", "Team", "Craft", "Bag", "Settings"};
#define MENU_COUNT (sizeof(menu_items) / sizeof(menu_items[0]))

static void draw_intro(App* app, Canvas* canvas) {
    Echoid* e = app_lead(app);
    EchoGenes g;
    echo_genes(e->mac, &g);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Your echo awakens!");
    draw_sprite(canvas, e->mac, 2, 16, 2);
    canvas_set_font(canvas, FontSecondary);
    char buf[32];
    canvas_draw_str(canvas, 40, 22, echo_species_name(e->mac));
    snprintf(buf, sizeof(buf), "%s - %s", echo_type_name(g.type), echo_rarity_name(g.rarity));
    canvas_draw_str(canvas, 40, 31, buf);
    canvas_draw_str(canvas, 40, 40, "Level 1");
    canvas_draw_str(canvas, 40, 49, "Born from your MAC");
    elements_button_center(canvas, "Start");
}

static void draw_menu(App* app, Canvas* canvas) {
    Echoid* e = app_lead(app);
    EchoStats s;
    echo_stats(e, &s);
    draw_title(canvas, "Echoids", NULL);
    for(size_t i = 0; i < MENU_COUNT; i++) {
        int y = 23 + (int)i * 10;
        draw_row(canvas, y, i == app->menu_idx);
        canvas_draw_str(canvas, 4, y, menu_items[i]);
    }
    canvas_set_color(canvas, ColorBlack);
    /* Lead echoid panel */
    canvas_draw_line(canvas, 84, 13, 84, 63);
    draw_sprite(canvas, e->mac, 90, 15, 2);
    char buf[24];
    snprintf(buf, sizeof(buf), "Lv%u", e->level);
    canvas_draw_str_aligned(canvas, 106, 55, AlignCenter, AlignBottom, buf);
    draw_hp_bar(canvas, 88, 58, 38, e->hp, s.max_hp);
}

/* Small echo ball: circle with a band and a centre dot. */
static void draw_ball_icon(Canvas* canvas, int x, int y) {
    canvas_draw_circle(canvas, x + 3, y + 3, 3);
    canvas_draw_line(canvas, x, y + 3, x + 6, y + 3);
    canvas_draw_dot(canvas, x + 3, y + 3);
}

/* Small shard: a diamond. */
static void draw_shard_icon(Canvas* canvas, int x, int y) {
    canvas_draw_line(canvas, x + 3, y, x + 6, y + 3);
    canvas_draw_line(canvas, x + 6, y + 3, x + 3, y + 6);
    canvas_draw_line(canvas, x + 3, y + 6, x, y + 3);
    canvas_draw_line(canvas, x, y + 3, x + 3, y);
    canvas_draw_line(canvas, x + 3, y + 2, x + 3, y + 4);
}

static void draw_collect(App* app, Canvas* canvas) {
    const char* title = "Idle";
    switch(scanner_phase(app->scanner)) {
    case ScanPhaseWifi:
        title = "WiFi";
        break;
    case ScanPhaseBt:
        title = "BT";
        break;
    default:
        break;
    }
    draw_title(canvas, title, NULL);

    /* Header counters: echoids spotted and shards collected this outing. */
    char num[8];
    canvas_set_font(canvas, FontSecondary);
    snprintf(num, sizeof(num), "%u", app->shards_this_run);
    int x = 127 - (int)canvas_string_width(canvas, num);
    canvas_draw_str(canvas, x, 10, num);
    x -= 9;
    draw_shard_icon(canvas, x, 3);
    x -= 6;
    snprintf(num, sizeof(num), "%u", app->spotted_count);
    x -= (int)canvas_string_width(canvas, num);
    canvas_draw_str(canvas, x, 10, num);
    x -= 9;
    draw_ball_icon(canvas, x, 3);

    if(app->nearby_count == 0) {
        const char* err = scanner_error(app->scanner);
        uint32_t waited = furi_get_tick() - app->scan_started;
        if(err) {
            canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignCenter, err);
            canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, "Settings > Board setup");
        } else if(scanner_booting(app->scanner)) {
            canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "Powering up board...");
        } else if(app->save.source == ScanSourceDemo) {
            canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "Listening for echoes...");
        } else {
            /* Explain exactly what the board is (not) doing. */
            ScanStats st;
            scanner_get_stats(app->scanner, &st);
            char info[40];
            const char* status;
            if(st.download_mode) {
                status = "Board is in flash mode!";
            } else if(st.reboots >= 3) {
                status = "Board keeps rebooting!";
            } else if(!scanner_board_alive(app->scanner)) {
                status = waited > furi_ms_to_ticks(BOARD_TIMEOUT_MS) ? "Board is silent" :
                                                                       "Waiting for board...";
            } else if(st.rx_garbage > st.rx_printable / 4) {
                status = "Garbled data: wrong baud?";
            } else if(scanner_phase(app->scanner) == ScanPhaseBt) {
                status = "BT scan, no networks yet";
            } else {
                status = "Board talks, no networks";
            }
            canvas_draw_str_aligned(canvas, 64, 19, AlignCenter, AlignCenter, status);
            snprintf(
                info,
                sizeof(info),
                "rx%lu ln%lu ap%lu bt%lu",
                (unsigned long)st.rx_bytes,
                (unsigned long)st.lines,
                (unsigned long)st.aps,
                (unsigned long)st.bts);
            canvas_draw_str_aligned(canvas, 64, 29, AlignCenter, AlignCenter, info);
            char last[SCAN_LOG_LEN * 2];
            scanner_last_rx(app->scanner, last, sizeof(last));
            if(st.download_mode) {
                canvas_draw_str_aligned(canvas, 64, 41, AlignCenter, AlignCenter, "Release BOOT / IO0, then");
                canvas_draw_str_aligned(canvas, 64, 51, AlignCenter, AlignCenter, "press RST on the board");
            } else if(st.reboots >= 3) {
                snprintf(info, sizeof(info), "%lu resets: firmware crash", (unsigned long)st.reboots);
                canvas_draw_str_aligned(canvas, 64, 41, AlignCenter, AlignCenter, info);
                canvas_draw_str_aligned(canvas, 64, 51, AlignCenter, AlignCenter, "or power. Reflash/USB test");
            } else if(last[0]) {
                canvas_set_font(canvas, FontKeyboard);
                canvas_draw_str(canvas, 0, 40, "Last line from board:");
                canvas_draw_str(canvas, 0, 48, last);
                if(strlen(last) > 21) canvas_draw_str(canvas, 0, 56, last + 21);
                canvas_set_font(canvas, FontSecondary);
            } else {
                canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignCenter, "Check Settings > Board");
                canvas_draw_str_aligned(canvas, 64, 52, AlignCenter, AlignCenter, "setup > UART terminal");
            }
        }
    }

    for(uint8_t row = 0; row < NEARBY_ROWS; row++) {
        uint8_t i = app->nearby_scroll + row;
        if(i >= app->nearby_count) break;
        Nearby* n = &app->nearby[i];
        int y = 23 + row * 10;
        draw_row(canvas, y, i == app->nearby_idx);
        EchoGenes g;
        echo_genes(n->mac, &g);
        char buf[24];
        snprintf(buf, sizeof(buf), "%s%s", nearby_state_mark(n->state), echo_species_name(n->mac));
        canvas_draw_str(canvas, 2, y, buf);
        snprintf(buf, sizeof(buf), "L%u", echo_wild_level(n->mac, app_lead(app)->level));
        canvas_draw_str(canvas, 58, y, buf);
        canvas_draw_str(canvas, 76, y, echo_type_short(g.type));
        snprintf(buf, sizeof(buf), "%d", n->rssi);
        canvas_draw_str_aligned(canvas, 121, y, AlignRight, AlignBottom, buf);
    }
    canvas_set_color(canvas, ColorBlack);
    if(app->nearby_count > NEARBY_ROWS) {
        elements_scrollbar_pos(canvas, 127, 14, 31, app->nearby_idx, app->nearby_count);
    }

    /* Stats panel for the selected echoid, at the level it would battle at. */
    if(app->nearby_count > 0 && app->nearby_idx < app->nearby_count) {
        Nearby* n = &app->nearby[app->nearby_idx];
        EchoGenes g;
        echo_genes(n->mac, &g);
        Echoid wild;
        echo_init(&wild, n->mac, echo_wild_level(n->mac, app_lead(app)->level), NULL);
        EchoStats st;
        echo_stats(&wild, &st);
        char buf[40];
        canvas_draw_line(canvas, 0, 45, 127, 45);
        snprintf(
            buf,
            sizeof(buf),
            "%s %s  Lv%u",
            echo_rarity_name(g.rarity),
            echo_type_name(g.type),
            wild.level);
        canvas_draw_str(canvas, 2, 54, buf);
        snprintf(
            buf, sizeof(buf), "HP%u ATK%u DEF%u SPD%u", st.max_hp, st.atk, st.def, st.spd);
        canvas_draw_str(canvas, 2, 63, buf);
    }
    draw_toast(app, canvas);
}

static void draw_battle(App* app, Canvas* canvas) {
    Battle* b = &app->battle;
    char buf[40];
    EchoStats sw, sm;
    echo_stats(&b->wild, &sw);
    echo_stats(b->mine, &sm);

    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "%s L%u", echo_species_name(b->wild.mac), b->wild.level);
    canvas_draw_str(canvas, 0, 8, buf);
    EchoGenes gw;
    echo_genes(b->wild.mac, &gw);
    snprintf(buf, sizeof(buf), "%s %s", echo_type_short(gw.type), echo_rarity_name(gw.rarity));
    canvas_draw_str(canvas, 0, 17, buf);
    draw_hp_bar(canvas, 0, 19, 60, b->wild.hp, sw.max_hp);
    draw_sprite(canvas, b->wild.mac, 94, 0, 2);

    snprintf(buf, sizeof(buf), "%s L%u", echo_species_name(b->mine->mac), b->mine->level);
    canvas_draw_str(canvas, 0, 32, buf);
    draw_hp_bar(canvas, 0, 34, 50, b->mine->hp, sm.max_hp);
    snprintf(buf, sizeof(buf), "%u/%u", b->mine->hp, sm.max_hp);
    canvas_draw_str(canvas, 53, 39, buf);

    canvas_draw_str(canvas, 0, 47, b->log[0]);
    canvas_draw_str(canvas, 0, 55, b->log[1]);

    if(b->result == BattleOngoing) {
        static const char* const opts[] = {"Hit", "Skill", "Ball", "Run"};
        for(int i = 0; i < 4; i++) {
            int x = i * 32;
            if(i == app->battle_menu) {
                canvas_draw_box(canvas, x, 56, 32, 8);
                canvas_set_color(canvas, ColorWhite);
            }
            canvas_draw_str_aligned(canvas, x + 16, 63, AlignCenter, AlignBottom, opts[i]);
            canvas_set_color(canvas, ColorBlack);
        }
    } else {
        canvas_draw_box(canvas, 0, 56, 128, 8);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, "OK: continue");
        canvas_set_color(canvas, ColorBlack);
    }
}

static void draw_ball_pick(App* app, Canvas* canvas) {
    draw_battle(app, canvas);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 14, 8, 100, 46);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, 14, 8, 100, 46, 3);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 17, AlignCenter, AlignBottom, "Throw which ball?");
    for(uint8_t i = 0; i <= ItemPrismBall; i++) {
        int y = 28 + i * 10;
        char buf[16];
        if(i == app->ball_idx) {
            canvas_draw_box(canvas, 17, y - 8, 94, 10);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, 19, y, item_name(i));
        snprintf(buf, sizeof(buf), "x%u", app->save.inv.items[i]);
        canvas_draw_str(canvas, 70, y, buf);
        snprintf(buf, sizeof(buf), "%u%%", echo_capture_chance(&app->battle.wild, i));
        canvas_draw_str_aligned(canvas, 109, y, AlignRight, AlignBottom, buf);
        canvas_set_color(canvas, ColorBlack);
    }
}

static void draw_team_list(
    App* app,
    Canvas* canvas,
    const char* title,
    const char* right,
    uint8_t sel,
    uint8_t scroll) {
    char buf[32];
    draw_title(canvas, title, right);
    for(uint8_t row = 0; row < TEAM_ROWS; row++) {
        uint8_t i = scroll + row;
        if(i >= app->save.team_count) break;
        Echoid* e = &app->save.team[i];
        EchoGenes g;
        echo_genes(e->mac, &g);
        EchoStats s;
        echo_stats(e, &s);
        int y = 22 + row * 10;
        draw_row(canvas, y, i == sel);
        snprintf(buf, sizeof(buf), "%s%s", i == app->save.lead ? ">" : " ", echo_species_name(e->mac));
        canvas_draw_str(canvas, 2, y, buf);
        snprintf(buf, sizeof(buf), "L%u", e->level);
        canvas_draw_str(canvas, 62, y, buf);
        canvas_draw_str(canvas, 80, y, echo_type_short(g.type));
        canvas_draw_str_aligned(canvas, 121, y, AlignRight, AlignBottom, e->hp ? "" : "KO");
        if(e->hp) {
            /* tiny hp bar */
            int fill = (int)(16u * e->hp / (s.max_hp ? s.max_hp : 1));
            canvas_draw_frame(canvas, 102, y - 6, 18, 5);
            if(fill > 0) canvas_draw_box(canvas, 103, y - 5, fill, 3);
        }
    }
    canvas_set_color(canvas, ColorBlack);
    if(app->save.team_count > TEAM_ROWS) {
        elements_scrollbar_pos(canvas, 127, 14, 48, sel, app->save.team_count);
    }
}

static void draw_team(App* app, Canvas* canvas) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u/%u", app->save.team_count, MAX_TEAM);
    draw_team_list(app, canvas, "Team", buf, app->team_idx, app->team_scroll);
}

static void draw_use_on(App* app, Canvas* canvas) {
    char buf[16];
    snprintf(buf, sizeof(buf), "x%u", app->save.inv.items[app->use_item_id]);
    draw_team_list(
        app,
        canvas,
        app->use_item_id == ItemTonic ? "Heal who?" : "Crystal on?",
        buf,
        app->use_idx,
        app->use_scroll);
    draw_toast(app, canvas);
}

static void draw_detail(App* app, Canvas* canvas) {
    Echoid* e = &app->save.team[app->team_idx];
    EchoGenes g;
    echo_genes(e->mac, &g);
    EchoStats s;
    echo_stats(e, &s);
    char buf[40];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 9, echo_species_name(e->mac));
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "%u/%u", app->team_idx + 1, app->save.team_count);
    canvas_draw_str_aligned(canvas, 127, 9, AlignRight, AlignBottom, buf);

    draw_sprite(canvas, e->mac, 0, 12, 2);
    snprintf(buf, sizeof(buf), "%s  %s", echo_type_name(g.type), echo_rarity_name(g.rarity));
    canvas_draw_str(canvas, 36, 19, buf);
    snprintf(buf, sizeof(buf), "Lv%u  XP %u/%u", e->level, e->xp, echo_xp_to_next(e->level));
    canvas_draw_str(canvas, 36, 28, buf);
    snprintf(buf, sizeof(buf), "HP %u/%u", e->hp, s.max_hp);
    canvas_draw_str(canvas, 36, 37, buf);
    snprintf(buf, sizeof(buf), "A%u D%u S%u", s.atk, s.def, s.spd);
    canvas_draw_str(canvas, 36, 46, buf);
    snprintf(buf, sizeof(buf), "From: %.20s", e->origin);
    canvas_draw_str(canvas, 0, 54, buf);
    canvas_draw_str(
        canvas, 0, 63, app->team_idx == app->save.lead ? "* Lead echoid  <> browse" : "OK: set lead  <> browse");
}

static void draw_craft(App* app, Canvas* canvas) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Shards %lu", (unsigned long)inventory_shard_total(&app->save.inv));
    draw_title(canvas, "Craft", buf);
    for(uint8_t row = 0; row < CRAFT_ROWS; row++) {
        uint8_t i = app->craft_scroll + row;
        if(i >= recipe_count) break;
        const Recipe* r = &recipes[i];
        int y = 22 + row * 10;
        draw_row(canvas, y, i == app->craft_idx);
        snprintf(buf, sizeof(buf), "%s%s", craft_can(&app->save.inv, i) ? "+" : " ", r->name);
        canvas_draw_str(canvas, 2, y, buf);
        snprintf(buf, sizeof(buf), "have %u", app->save.inv.items[r->out]);
        canvas_draw_str_aligned(canvas, 121, y, AlignRight, AlignBottom, buf);
    }
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_line(canvas, 0, 46, 127, 46);
    canvas_draw_str(canvas, 2, 55, recipes[app->craft_idx].recipe);
    canvas_draw_str(canvas, 2, 63, craft_can(&app->save.inv, app->craft_idx) ? "OK: craft" : "Not enough shards");
    elements_scrollbar_pos(canvas, 127, 14, 31, app->craft_idx, recipe_count);
    draw_toast(app, canvas);
}

static void draw_bag(App* app, Canvas* canvas) {
    char buf[32];
    draw_title(canvas, app->bag_tab == 0 ? "Bag: Items" : "Bag: Shards", "<  >");
    if(app->bag_tab == 0) {
        for(uint8_t i = 0; i < ItemCount; i++) {
            int y = 22 + i * 9;
            draw_row(canvas, y, i == app->bag_idx);
            canvas_draw_str(canvas, 2, y, item_name(i));
            snprintf(buf, sizeof(buf), "x%u", app->save.inv.items[i]);
            canvas_draw_str_aligned(canvas, 121, y, AlignRight, AlignBottom, buf);
        }
        canvas_set_color(canvas, ColorBlack);
    } else {
        canvas_draw_str(canvas, 2, 21, "Dull/Bright/Prismatic");
        for(uint8_t t = 0; t < ECHO_TYPE_COUNT; t++) {
            int x = (t / 4) * 64 + 2;
            int y = 31 + (t % 4) * 9;
            snprintf(
                buf,
                sizeof(buf),
                "%s %u/%u/%u",
                echo_type_short(t),
                app->save.inv.shards[t][0],
                app->save.inv.shards[t][1],
                app->save.inv.shards[t][2]);
            canvas_draw_str(canvas, x, y, buf);
        }
    }
    draw_toast(app, canvas);
}

#define SETTINGS_COUNT 3

static void draw_settings(App* app, Canvas* canvas) {
    draw_title(canvas, "Settings", NULL);
    char l0[32];
    snprintf(
        l0,
        sizeof(l0),
        "Board setup (%s)",
        app->save.source == ScanSourceDemo ? "Demo" : "ESP32");
    const char* lines[SETTINGS_COUNT] = {
        l0,
        "How to play",
        app->confirm_reset ? "Reset? OK again!" : "Reset game",
    };
    for(uint8_t i = 0; i < SETTINGS_COUNT; i++) {
        int y = 22 + i * 10;
        draw_row(canvas, y, i == app->settings_idx);
        canvas_draw_str(canvas, 4, y, lines[i]);
    }
    canvas_set_color(canvas, ColorBlack);
}

static const char* const help_lines[] = {
    "HOW TO PLAY",
    "Collect: scans WiFi + BT",
    "via an ESP32 board running",
    "Marauder on the GPIO pins.",
    "",
    "WiFi access points are wild",
    "echoids. The MAC decides",
    "species, type and stats.",
    "Pick one and press OK to",
    "battle it.",
    "",
    "Bluetooth devices drop one",
    "shard each (MAC decides",
    "element and tier).",
    "",
    "Craft Echo Balls from",
    "shards. Weaken a wild echoid",
    "then throw a ball to catch.",
    "",
    "Types: each beats the next",
    "Tide>Ember>Flora>Stone>",
    "Spark>Gale>Pulse>Void>Tide",
    "Skills use type, Hit doesn't.",
    "",
    "Win or catch to gain XP.",
    "HP regenerates slowly, or",
    "use a Tonic from the Bag.",
    "* owned  x already fought",
    "Top right: echoids spotted",
    "(ball) and shards found",
    "(diamond) since app start.",
    "",
    "BOARD NOT WORKING?",
    "Settings > Board setup >",
    "Terminal shows raw UART.",
    "Up/Down there changes the",
    "UART pins / baud live.",
    "Auto-calibrate tries all.",
    "Wire board TX->Flipper RX.",
};
#define HELP_COUNT (sizeof(help_lines) / sizeof(help_lines[0]))
#define HELP_ROWS  6

static void draw_help(App* app, Canvas* canvas) {
    canvas_set_font(canvas, FontSecondary);
    for(uint8_t row = 0; row < HELP_ROWS; row++) {
        uint8_t i = app->help_scroll + row;
        if(i >= HELP_COUNT) break;
        canvas_draw_str(canvas, 2, 9 + row * 10, help_lines[i]);
    }
    elements_scrollbar(canvas, app->help_scroll, HELP_COUNT - HELP_ROWS + 1);
}

static void draw_board(App* app, Canvas* canvas);
static void draw_terminal(App* app, Canvas* canvas);
static void draw_calibrate(App* app, Canvas* canvas);

static void draw_callback(Canvas* canvas, void* ctx) {
    App* app = ctx;
    if(furi_mutex_acquire(app->mutex, 25) != FuriStatusOk) return;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    switch(app->screen) {
    case ScreenIntro:
        draw_intro(app, canvas);
        break;
    case ScreenMenu:
        draw_menu(app, canvas);
        break;
    case ScreenCollect:
        draw_collect(app, canvas);
        break;
    case ScreenBattle:
        draw_battle(app, canvas);
        break;
    case ScreenBallPick:
        draw_ball_pick(app, canvas);
        break;
    case ScreenTeam:
        draw_team(app, canvas);
        break;
    case ScreenDetail:
        draw_detail(app, canvas);
        break;
    case ScreenCraft:
        draw_craft(app, canvas);
        break;
    case ScreenBag:
        draw_bag(app, canvas);
        break;
    case ScreenSettings:
        draw_settings(app, canvas);
        break;
    case ScreenHelp:
        draw_help(app, canvas);
        break;
    case ScreenBoard:
        draw_board(app, canvas);
        break;
    case ScreenTerminal:
        draw_terminal(app, canvas);
        break;
    case ScreenCalibrate:
        draw_calibrate(app, canvas);
        break;
    case ScreenUseOn:
        draw_use_on(app, canvas);
        break;
    }
    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* event, void* ctx) {
    App* app = ctx;
    furi_message_queue_put(app->queue, event, FuriWaitForever);
}

/* ------------------------------------------------------------------------- */
/* Game actions                                                              */
/* ------------------------------------------------------------------------- */

static void list_move(uint8_t* idx, uint8_t* scroll, uint8_t count, uint8_t rows, int delta) {
    if(count == 0) {
        *idx = 0;
        *scroll = 0;
        return;
    }
    int n = (int)*idx + delta;
    if(n < 0) n = count - 1;
    if(n >= count) n = 0;
    *idx = (uint8_t)n;
    if(*idx < *scroll) *scroll = *idx;
    if(*idx >= *scroll + rows) *scroll = *idx - rows + 1;
}

/* ------------------------------------------------------------------------- */
/* Board setup, terminal and calibration                                     */
/* ------------------------------------------------------------------------- */

enum {
    BoardSource,
    BoardUart,
    BoardBaud,
    BoardPower,
    BoardBoot,
    BoardEol,
    BoardWifiCmd,
    BoardBtCmd,
    BoardBt,
    BoardLog,
    BoardTerminal,
    BoardCalibrate,
    BoardCount,
};
#define BOARD_ROWS 5

static const char* const term_cmds[] = {
    "help",
    "scanall",
    "sniffbt",
    "stopscan",
    "scanap",
    "list -a",
    "clearlist -a",
    "reboot",
};
#define TERM_CMD_COUNT (sizeof(term_cmds) / sizeof(term_cmds[0]))

static const char* uart_short(uint8_t uart) {
    return uart == ScanUartLpuart ? "L15/16" : "U13/14";
}

static void board_row_text(App* app, uint8_t row, char* buf, size_t len) {
    SaveData* sv = &app->save;
    switch(row) {
    case BoardSource:
        snprintf(buf, len, "Source: %s", sv->source == ScanSourceDemo ? "Demo" : "ESP32 board");
        break;
    case BoardUart:
        snprintf(buf, len, "Pins: %s", scanner_uart_name(sv->uart % ScanUartCount));
        break;
    case BoardBaud:
        snprintf(
            buf, len, "Baud: %lu", (unsigned long)scanner_baud_rates[sv->baud_idx % scanner_baud_count]);
        break;
    case BoardPower:
        snprintf(buf, len, "5V power: %s", scanner_power_name(sv->power % ScanPowerCount));
        break;
    case BoardBoot:
        snprintf(
            buf,
            len,
            "Boot wait: %u ms",
            scanner_boot_waits_ms[sv->boot_idx % scanner_boot_count]);
        break;
    case BoardEol:
        snprintf(buf, len, "Line end: %s", sv->eol ? "CR+LF" : "LF");
        break;
    case BoardWifiCmd:
        snprintf(buf, len, "WiFi cmd: %s", scanner_wifi_cmds[sv->wifi_cmd % scanner_wifi_cmd_count]);
        break;
    case BoardBtCmd:
        snprintf(buf, len, "BT cmd: %s", scanner_bt_cmds[sv->bt_cmd % scanner_bt_cmd_count]);
        break;
    case BoardBt:
        snprintf(buf, len, "Bluetooth scan: %s", sv->bt_enabled ? "On" : "Off");
        break;
    case BoardLog:
        snprintf(buf, len, "Log UART to SD: %s", sv->log_sd ? "On" : "Off");
        break;
    case BoardTerminal:
        snprintf(buf, len, "> UART terminal");
        break;
    case BoardCalibrate:
        snprintf(buf, len, "> Auto-calibrate");
        break;
    default:
        buf[0] = '\0';
    }
}

static uint8_t cycle(uint8_t value, size_t count, int delta) {
    return (uint8_t)(((int)(value % count) + (int)count + delta) % (int)count);
}

static void board_change(App* app, uint8_t row, int delta) {
    SaveData* sv = &app->save;
    switch(row) {
    case BoardSource:
        sv->source = sv->source == ScanSourceDemo ? ScanSourceBoard : ScanSourceDemo;
        break;
    case BoardUart:
        sv->uart = cycle(sv->uart, ScanUartCount, delta);
        break;
    case BoardBaud:
        sv->baud_idx = cycle(sv->baud_idx, scanner_baud_count, delta);
        break;
    case BoardPower:
        sv->power = cycle(sv->power, ScanPowerCount, delta);
        break;
    case BoardBoot:
        sv->boot_idx = cycle(sv->boot_idx, scanner_boot_count, delta);
        break;
    case BoardEol:
        sv->eol = !sv->eol;
        break;
    case BoardWifiCmd:
        sv->wifi_cmd = cycle(sv->wifi_cmd, scanner_wifi_cmd_count, delta);
        break;
    case BoardBtCmd:
        sv->bt_cmd = cycle(sv->bt_cmd, scanner_bt_cmd_count, delta);
        break;
    case BoardBt:
        sv->bt_enabled = !sv->bt_enabled;
        break;
    case BoardLog:
        sv->log_sd = !sv->log_sd;
        if(sv->log_sd) app_toast(app, "Saves to apps_data/echoids");
        break;
    default:
        return;
    }
    app->dirty = true;
}

static void draw_board(App* app, Canvas* canvas) {
    draw_title(canvas, "Board setup", "<> change");
    char buf[32];
    for(uint8_t r = 0; r < BOARD_ROWS; r++) {
        uint8_t i = app->board_scroll + r;
        if(i >= BoardCount) break;
        int y = 22 + r * 10;
        draw_row(canvas, y, i == app->board_idx);
        board_row_text(app, i, buf, sizeof(buf));
        canvas_draw_str(canvas, 3, y, buf);
    }
    canvas_set_color(canvas, ColorBlack);
    elements_scrollbar_pos(canvas, 127, 14, 48, app->board_idx, BoardCount);
    draw_toast(app, canvas);
}

/* --- terminal --- */

static void terminal_open(App* app) {
    ScanConfig cfg;
    app_scan_config(app, &cfg);
    scanner_start_manual(app->scanner, &cfg);
}

static void terminal_close(App* app) {
    scanner_stop(app->scanner);
    if(app->term_otg_by_us) {
        scanner_power_off();
        app->term_otg_by_us = false;
    }
}

static void draw_terminal(App* app, Canvas* canvas) {
    char buf[40];
    ScanStats st;
    scanner_get_stats(app->scanner, &st);

    canvas_set_font(canvas, FontSecondary);
    snprintf(
        buf,
        sizeof(buf),
        "^%s v%lu",
        uart_short(app->save.uart),
        (unsigned long)scanner_baud_rates[app->save.baud_idx % scanner_baud_count]);
    canvas_draw_str(canvas, 0, 7, buf);
    canvas_draw_str_aligned(
        canvas, 127, 7, AlignRight, AlignBottom, furi_hal_power_is_otg_enabled() ? "5V on" : "5V off");

    canvas_set_font(canvas, FontKeyboard);
    if(st.download_mode) {
        snprintf(buf, sizeof(buf), "FLASH MODE: press RST");
    } else if(st.reboots >= 3) {
        snprintf(buf, sizeof(buf), "REBOOT LOOP x%lu", (unsigned long)st.reboots);
    } else {
        snprintf(
            buf,
            sizeof(buf),
            "rx%lu bad%lu ap%lu bt%lu",
            (unsigned long)st.rx_bytes,
            (unsigned long)st.rx_garbage,
            (unsigned long)st.aps,
            (unsigned long)st.bts);
    }
    canvas_draw_str(canvas, 0, 15, buf);
    canvas_draw_line(canvas, 0, 17, 127, 17);

    const char* err = scanner_error(app->scanner);
    if(!scanner_is_running(app->scanner)) {
        canvas_draw_str(canvas, 0, 26, err ? err : "UART closed");
    } else {
        char log[SCAN_LOG_LINES][SCAN_LOG_LEN];
        size_t n = scanner_get_log(app->scanner, log);
        size_t first = n > 5 ? n - 5 : 0;
        for(size_t i = first; i < n; i++) {
            canvas_draw_str(canvas, 0, 25 + (int)(i - first) * 7, log[i]);
        }
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_box(canvas, 0, 55, 128, 9);
    canvas_set_color(canvas, ColorWhite);
    snprintf(buf, sizeof(buf), "< %s >  OK:send", term_cmds[app->term_cmd % TERM_CMD_COUNT]);
    canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, buf);
    canvas_set_color(canvas, ColorBlack);
}

static void terminal_input(App* app, InputEvent* ev, bool press) {
    if(press && ev->key == InputKeyLeft) {
        app->term_cmd = cycle(app->term_cmd, TERM_CMD_COUNT, -1);
    } else if(press && ev->key == InputKeyRight) {
        app->term_cmd = cycle(app->term_cmd, TERM_CMD_COUNT, 1);
    } else if(press && ev->key == InputKeyOk) {
        scanner_send_cmd(app->scanner, term_cmds[app->term_cmd % TERM_CMD_COUNT]);
    } else if(ev->type == InputTypeLong && ev->key == InputKeyOk) {
        /* Manual 5V toggle for debugging power issues. */
        if(furi_hal_power_is_otg_enabled()) {
            scanner_power_off();
            app->term_otg_by_us = false;
        } else {
            app->term_otg_by_us = scanner_power_on();
        }
    } else if(press && (ev->key == InputKeyUp || ev->key == InputKeyDown)) {
        /* Change pins / baud on the fly and reopen the UART. */
        if(ev->key == InputKeyUp)
            app->save.uart = cycle(app->save.uart, ScanUartCount, 1);
        else
            app->save.baud_idx = cycle(app->save.baud_idx, scanner_baud_count, 1);
        app->dirty = true;
        terminal_open(app);
    } else if(press && ev->key == InputKeyBack) {
        terminal_close(app);
        app->screen = ScreenBoard;
        if(app->dirty) app_save(app);
    }
}

/* --- calibration: try every pin / baud combination and keep the best --- */

#define CAL_CANDIDATES  ((uint8_t)(ScanUartCount * scanner_baud_count))
#define CAL_LISTEN_MS   1500
#define CAL_GOOD_SCORE  40
#define CAL_GREAT_SCORE 400

static bool calibrate_done(App* app) {
    return app->cal_step > CAL_CANDIDATES;
}

static void calibrate_candidate(uint8_t k, uint8_t* uart, uint8_t* baud_idx) {
    *uart = k / scanner_baud_count;
    *baud_idx = k % scanner_baud_count;
}

static void calibrate_start(App* app) {
    scanner_stop(app->scanner);
    app->cal_step = 0;
    app->cal_best = -1;
    app->cal_best_score = INT32_MIN;
    app->cal_result[0] = '\0';
    app->cal_otg_by_us = false;
    uint32_t wait = 0;
    if(app->save.power % ScanPowerCount != ScanPowerUntouched) {
        app->cal_otg_by_us = scanner_power_on();
        if(app->cal_otg_by_us) wait = scanner_boot_waits_ms[app->save.boot_idx % scanner_boot_count];
    }
    app->cal_tick = furi_get_tick() + furi_ms_to_ticks(wait);
}

static void calibrate_stop(App* app) {
    scanner_stop(app->scanner);
    if(app->cal_otg_by_us && app->save.power % ScanPowerCount == ScanPowerAuto) {
        scanner_power_off();
    }
    app->cal_otg_by_us = false;
}

static void calibrate_open(App* app) {
    ScanConfig cfg;
    app_scan_config(app, &cfg);
    calibrate_candidate(app->cal_step - 1, &cfg.uart, &cfg.baud_idx);
    cfg.power = ScanPowerUntouched; /* power is handled by calibration itself */
    app->cal_sent = false;
    app->cal_tick = furi_get_tick();
    scanner_start_manual(app->scanner, &cfg);
}

static void calibrate_finish(App* app) {
    app->cal_step = CAL_CANDIDATES + 1;
    scanner_stop(app->scanner);
    if(app->cal_best >= 0 && app->cal_best_score >= CAL_GOOD_SCORE) {
        uint8_t uart, baud;
        calibrate_candidate((uint8_t)app->cal_best, &uart, &baud);
        app->save.uart = uart;
        app->save.baud_idx = baud;
        app->save.source = ScanSourceBoard;
        app->dirty = true;
        app_save(app);
        snprintf(
            app->cal_result,
            sizeof(app->cal_result),
            "%s @%lu",
            uart_short(uart),
            (unsigned long)scanner_baud_rates[baud]);
        notification_message(app->notif, &sequence_success);
    } else {
        app->cal_result[0] = '\0';
        notification_message(app->notif, &sequence_error);
    }
    if(app->cal_otg_by_us && app->save.power % ScanPowerCount == ScanPowerAuto) {
        scanner_power_off();
        app->cal_otg_by_us = false;
    }
}

static void calibrate_tick(App* app) {
    if(calibrate_done(app)) return;
    uint32_t now = furi_get_tick();
    if(app->cal_step == 0) {
        if((int32_t)(now - app->cal_tick) >= 0) {
            app->cal_step = 1;
            calibrate_open(app);
        }
        return;
    }
    if(!scanner_is_running(app->scanner)) {
        /* Could not open this combination (busy / unsupported baud): skip it. */
        if(++app->cal_step > CAL_CANDIDATES)
            calibrate_finish(app);
        else
            calibrate_open(app);
        return;
    }
    if(!app->cal_sent && now - app->cal_tick > furi_ms_to_ticks(150)) {
        scanner_send_cmd(app->scanner, "help");
        app->cal_sent = true;
    }
    if(now - app->cal_tick < furi_ms_to_ticks(CAL_LISTEN_MS)) return;

    ScanStats st;
    scanner_get_stats(app->scanner, &st);
    int32_t score = (int32_t)st.rx_printable + 40 * (int32_t)st.lines - 8 * (int32_t)st.rx_garbage;
    if(st.rx_bytes > 0 && score > app->cal_best_score) {
        app->cal_best_score = score;
        app->cal_best = (int16_t)(app->cal_step - 1);
    }
    scanner_stop(app->scanner);
    if(score >= CAL_GREAT_SCORE || app->cal_step >= CAL_CANDIDATES) {
        calibrate_finish(app);
    } else {
        app->cal_step++;
        calibrate_open(app);
    }
}

static void draw_calibrate(App* app, Canvas* canvas) {
    draw_title(canvas, "Auto-calibrate", NULL);
    char buf[40];
    if(calibrate_done(app)) {
        if(app->cal_result[0]) {
            canvas_draw_str(canvas, 2, 24, "Board found and saved:");
            canvas_set_font(canvas, FontPrimary);
            canvas_draw_str(canvas, 2, 36, app->cal_result);
            canvas_set_font(canvas, FontSecondary);
        } else {
            canvas_draw_str(canvas, 2, 22, "No board answered.");
            canvas_draw_str(canvas, 2, 31, "Check: Marauder flashed,");
            canvas_draw_str(canvas, 2, 40, "board TX->Flipper RX, GND,");
            canvas_draw_str(canvas, 2, 49, "power (5V setting).");
        }
        elements_button_center(canvas, "OK");
        return;
    }
    if(app->cal_step == 0) {
        canvas_draw_str(canvas, 2, 26, "Powering board, waiting");
        canvas_draw_str(canvas, 2, 36, "for it to boot...");
    } else {
        uint8_t uart, baud;
        calibrate_candidate(app->cal_step - 1, &uart, &baud);
        snprintf(
            buf,
            sizeof(buf),
            "Trying %s @%lu",
            uart_short(uart),
            (unsigned long)scanner_baud_rates[baud]);
        canvas_draw_str(canvas, 2, 24, buf);
        ScanStats st;
        scanner_get_stats(app->scanner, &st);
        snprintf(
            buf,
            sizeof(buf),
            "rx %lu  bad %lu  lines %lu",
            (unsigned long)st.rx_bytes,
            (unsigned long)st.rx_garbage,
            (unsigned long)st.lines);
        canvas_draw_str(canvas, 2, 34, buf);
        if(app->cal_best >= 0) {
            calibrate_candidate((uint8_t)app->cal_best, &uart, &baud);
            snprintf(
                buf,
                sizeof(buf),
                "Best: %s @%lu",
                uart_short(uart),
                (unsigned long)scanner_baud_rates[baud]);
            canvas_draw_str(canvas, 2, 44, buf);
        }
        elements_progress_bar(
            canvas, 2, 50, 124, (float)(app->cal_step - 1) / (float)CAL_CANDIDATES);
    }
}

static void board_input(App* app, InputEvent* ev, bool press, int dy) {
    if(dy) {
        list_move(&app->board_idx, &app->board_scroll, BoardCount, BOARD_ROWS, dy);
    } else if(press && (ev->key == InputKeyLeft || ev->key == InputKeyRight)) {
        board_change(app, app->board_idx, ev->key == InputKeyLeft ? -1 : 1);
    } else if(press && ev->key == InputKeyOk) {
        if(app->board_idx == BoardTerminal) {
            if(app->save.source == ScanSourceDemo) {
                app_toast(app, "Set Source to ESP32 first");
            } else {
                app->term_otg_by_us = false;
                terminal_open(app);
                app->screen = ScreenTerminal;
            }
        } else if(app->board_idx == BoardCalibrate) {
            calibrate_start(app);
            app->screen = ScreenCalibrate;
        } else {
            board_change(app, app->board_idx, 1);
        }
    } else if(press && ev->key == InputKeyBack) {
        app->screen = ScreenSettings;
        if(app->dirty) app_save(app);
    }
}

static void collect_start(App* app) {
    for(uint8_t i = 0; i < app->nearby_count; i++) {
        if(app->nearby[i].state == NearbyWild && app_owns(app, app->nearby[i].mac))
            app->nearby[i].state = NearbyOwned;
    }
    app->scan_started = furi_get_tick();
    ScanConfig cfg;
    app_scan_config(app, &cfg);
    if(!scanner_start(app->scanner, (ScanSource)app->save.source, &cfg)) {
        app_toast(app, scanner_error(app->scanner) ? scanner_error(app->scanner) : "Scanner error");
        notification_message(app->notif, &sequence_error);
    }
    app->screen = ScreenCollect;
}

static void collect_stop(App* app) {
    scanner_stop(app->scanner);
    app->screen = ScreenMenu;
    if(app->dirty) app_save(app);
}

static void handle_ap(App* app, const ScanEvent* ev) {
    for(uint8_t i = 0; i < app->nearby_count; i++) {
        if(memcmp(app->nearby[i].mac, ev->mac, 6) == 0) {
            app->nearby[i].rssi = ev->rssi;
            return;
        }
    }
    Nearby* n;
    if(app->nearby_count < NEARBY_MAX) {
        n = &app->nearby[app->nearby_count++];
    } else {
        /* Recycle slots round-robin, but never the one in battle. */
        uint8_t slot = app->nearby_next_replace;
        if((int8_t)slot == app->battle_nearby && app->screen >= ScreenBattle &&
           app->screen <= ScreenBallPick) {
            slot = (slot + 1) % NEARBY_MAX;
        }
        app->nearby_next_replace = (slot + 1) % NEARBY_MAX;
        n = &app->nearby[slot];
    }
    memcpy(n->mac, ev->mac, 6);
    n->rssi = ev->rssi;
    strncpy(n->ssid, ev->name[0] ? ev->name : "<hidden>", NAME_LEN - 1);
    n->ssid[NAME_LEN - 1] = '\0';
    n->state = app_owns(app, ev->mac) ? NearbyOwned : NearbyWild;

    /* Count each echoid once per outing, even if its list slot was recycled. */
    uint32_t h = echo_hash_mac(ev->mac);
    bool counted = false;
    uint16_t known = app->spotted_count < SPOTTED_MAX ? app->spotted_count : SPOTTED_MAX;
    for(uint16_t i = 0; i < known && !counted; i++)
        counted = app->spotted[i] == h;
    if(!counted) {
        if(app->spotted_count < SPOTTED_MAX) app->spotted[app->spotted_count] = h;
        if(app->spotted_count < UINT16_MAX) app->spotted_count++;
    }
    if(n->state == NearbyWild) notification_message(app->notif, &sequence_blink_green_10);
}

static void handle_bt(App* app, const ScanEvent* ev) {
    SaveData* sv = &app->save;
    uint32_t h = echo_hash_mac(ev->mac);
    for(uint16_t i = 0; i < sv->seen_bt_count; i++) {
        if(sv->seen_bt[i] == h) return; /* each device gives its shard once */
    }
    sv->seen_bt[sv->seen_bt_pos] = h;
    sv->seen_bt_pos = (sv->seen_bt_pos + 1) % SEEN_BT_MAX;
    if(sv->seen_bt_count < SEEN_BT_MAX) sv->seen_bt_count++;

    Shard s = shard_from_mac(ev->mac);
    if(sv->inv.shards[s.type][s.tier] < 9999) sv->inv.shards[s.type][s.tier]++;
    sv->shards_found++;
    if(app->shards_this_run < UINT16_MAX) app->shards_this_run++;
    app->dirty = true;

    char msg[40];
    snprintf(msg, sizeof(msg), "+1 %s %s shard", shard_tier_name(s.tier), echo_type_name(s.type));
    app_toast(app, msg);
    notification_message(
        app->notif, s.tier == ShardPrismatic ? &sequence_blink_magenta_100 : &sequence_blink_cyan_10);
}

static void battle_begin(App* app) {
    if(app->nearby_count == 0) return;
    Nearby* n = &app->nearby[app->nearby_idx];
    if(n->state == NearbyOwned) {
        app_toast(app, "Already in your team");
        return;
    }
    if(n->state == NearbyDone) {
        app_toast(app, "Already fought - find others");
        return;
    }
    Echoid* lead = app_lead(app);
    if(lead->hp == 0) {
        app_toast(app, "Lead fainted! Heal or swap");
        return;
    }
    battle_start(&app->battle, lead, n->mac, echo_wild_level(n->mac, lead->level), n->ssid);
    app->battle_nearby = (int8_t)app->nearby_idx;
    app->battle_menu = 0;
    app->screen = ScreenBattle;
    notification_message(app->notif, &sequence_single_vibro);
}

static void battle_log_line(Battle* b, const char* msg) {
    memmove(b->log[0], b->log[1], BATTLE_LOG_LEN);
    strncpy(b->log[1], msg, BATTLE_LOG_LEN - 1);
    b->log[1][BATTLE_LOG_LEN - 1] = '\0';
}

static void battle_after_action(App* app) {
    Battle* b = &app->battle;
    if(b->result == BattleOngoing) return;
    char msg[BATTLE_LOG_LEN];
    Nearby* n = (app->battle_nearby >= 0 && app->battle_nearby < app->nearby_count) ?
                    &app->nearby[app->battle_nearby] :
                    NULL;
    if(n && memcmp(n->mac, b->wild.mac, 6) != 0) n = NULL;

    switch(b->result) {
    case BattleCaptured: {
        SaveData* sv = &app->save;
        Echoid caught = b->wild;
        if(caught.hp == 0) caught.hp = 1;
        if(sv->team_count < MAX_TEAM) sv->team[sv->team_count++] = caught;
        sv->captures++;
        if(n) n->state = NearbyOwned;
        notification_message(app->notif, &sequence_success);
        break;
    }
    case BattleWon:
        app->save.wins++;
        if(n) n->state = NearbyDone;
        notification_message(app->notif, &sequence_semi_success);
        break;
    case BattleFled:
        if(n) n->state = NearbyDone;
        break;
    case BattleLost:
        notification_message(app->notif, &sequence_error);
        break;
    default:
        break;
    }
    if(b->xp_gained) {
        if(b->levels_gained) {
            snprintf(
                msg,
                sizeof(msg),
                "+%lu XP  Level up! Lv%u",
                (unsigned long)b->xp_gained,
                b->mine->level);
        } else {
            snprintf(msg, sizeof(msg), "+%lu XP", (unsigned long)b->xp_gained);
        }
        battle_log_line(b, msg);
    }
    app->dirty = true;
}

/* Uses a Tonic or Echo Crystal on one team member. Returns true if used. */
static bool use_item_on(App* app, uint8_t item, Echoid* target) {
    Inventory* inv = &app->save.inv;
    EchoStats s;
    echo_stats(target, &s);
    const char* name = echo_species_name(target->mac);
    char msg[40];
    if(inv->items[item] == 0) {
        app_toast(app, "You have none");
        return false;
    }
    if(item == ItemTonic) {
        if(target->hp >= s.max_hp) {
            snprintf(msg, sizeof(msg), "%s is at full HP", name);
            app_toast(app, msg);
            return false;
        }
        target->hp = s.max_hp;
        inv->items[item]--;
        snprintf(msg, sizeof(msg), "%s fully healed!", name);
        app_toast(app, msg);
    } else if(item == ItemCrystal) {
        if(target->level >= MAX_LEVEL) {
            snprintf(msg, sizeof(msg), "%s is max level", name);
            app_toast(app, msg);
            return false;
        }
        inv->items[item]--;
        uint16_t xp = echo_xp_to_next(target->level) / 2;
        uint8_t lv = echo_add_xp(target, xp);
        if(lv)
            snprintf(msg, sizeof(msg), "%s +%u XP! Lv%u", name, xp, target->level);
        else
            snprintf(msg, sizeof(msg), "%s +%u XP", name, xp);
        app_toast(app, msg);
    } else {
        return false;
    }
    notification_message(app->notif, &sequence_success);
    app->dirty = true;
    return true;
}

/* ------------------------------------------------------------------------- */
/* Input                                                                     */
/* ------------------------------------------------------------------------- */

static void handle_input(App* app, InputEvent* ev) {
    bool nav = ev->type == InputTypeShort || ev->type == InputTypeRepeat;
    bool press = ev->type == InputTypeShort;
    int dy = 0;
    if(nav && ev->key == InputKeyUp) dy = -1;
    if(nav && ev->key == InputKeyDown) dy = 1;

    switch(app->screen) {
    case ScreenIntro:
        if(press && (ev->key == InputKeyOk || ev->key == InputKeyBack)) app->screen = ScreenMenu;
        break;

    case ScreenMenu:
        if(dy) {
            int n = (int)app->menu_idx + dy;
            app->menu_idx = (uint8_t)((n + (int)MENU_COUNT) % (int)MENU_COUNT);
        } else if(press && ev->key == InputKeyOk) {
            switch(app->menu_idx) {
            case 0:
                collect_start(app);
                break;
            case 1:
                app->team_idx = app->save.lead;
                app->team_scroll = 0;
                list_move(&app->team_idx, &app->team_scroll, app->save.team_count, TEAM_ROWS, 0);
                app->screen = ScreenTeam;
                break;
            case 2:
                app->screen = ScreenCraft;
                break;
            case 3:
                app->screen = ScreenBag;
                break;
            case 4:
                app->confirm_reset = false;
                app->screen = ScreenSettings;
                break;
            }
        } else if(press && ev->key == InputKeyBack) {
            app->running = false;
        }
        break;

    case ScreenCollect:
        if(dy) {
            list_move(&app->nearby_idx, &app->nearby_scroll, app->nearby_count, NEARBY_ROWS, dy);
        } else if(press && ev->key == InputKeyOk) {
            battle_begin(app);
        } else if(press && ev->key == InputKeyBack) {
            collect_stop(app);
        }
        break;

    case ScreenBattle:
        if(app->battle.result != BattleOngoing) {
            if(press && (ev->key == InputKeyOk || ev->key == InputKeyBack)) {
                app->screen = ScreenCollect;
                if(app->dirty) app_save(app);
            }
            break;
        }
        if(press && ev->key == InputKeyLeft) app->battle_menu = (app->battle_menu + 3) % 4;
        if(press && ev->key == InputKeyRight) app->battle_menu = (app->battle_menu + 1) % 4;
        if(press && ev->key == InputKeyOk) {
            switch(app->battle_menu) {
            case 0:
                battle_attack(&app->battle, false);
                break;
            case 1:
                battle_attack(&app->battle, true);
                break;
            case 2:
                if(app->save.team_count >= MAX_TEAM) {
                    battle_log_line(&app->battle, "Team is full!");
                } else {
                    app->screen = ScreenBallPick;
                }
                break;
            case 3:
                battle_flee(&app->battle);
                break;
            }
            battle_after_action(app);
        }
        break;

    case ScreenBallPick:
        if(dy) app->ball_idx = (uint8_t)((app->ball_idx + 3 + dy) % 3);
        if(press && ev->key == InputKeyBack) app->screen = ScreenBattle;
        if(press && ev->key == InputKeyOk) {
            if(!battle_throw(&app->battle, &app->save.inv, app->ball_idx)) {
                battle_log_line(&app->battle, "No balls of that kind!");
            }
            app->dirty = true;
            app->screen = ScreenBattle;
            battle_after_action(app);
        }
        break;

    case ScreenTeam:
        if(dy) {
            list_move(&app->team_idx, &app->team_scroll, app->save.team_count, TEAM_ROWS, dy);
        } else if(press && ev->key == InputKeyOk) {
            app->screen = ScreenDetail;
        } else if(press && ev->key == InputKeyBack) {
            app->screen = ScreenMenu;
        }
        break;

    case ScreenDetail:
        if(press && (ev->key == InputKeyLeft || ev->key == InputKeyRight)) {
            list_move(
                &app->team_idx,
                &app->team_scroll,
                app->save.team_count,
                TEAM_ROWS,
                ev->key == InputKeyLeft ? -1 : 1);
        } else if(press && ev->key == InputKeyOk) {
            if(app->save.lead != app->team_idx) {
                app->save.lead = app->team_idx;
                app->dirty = true;
                notification_message(app->notif, &sequence_blink_green_10);
            }
        } else if(press && ev->key == InputKeyBack) {
            app->screen = ScreenTeam;
        }
        break;

    case ScreenCraft:
        if(dy) {
            list_move(&app->craft_idx, &app->craft_scroll, recipe_count, CRAFT_ROWS, dy);
        } else if(press && ev->key == InputKeyOk) {
            if(craft_do(&app->save.inv, app->craft_idx)) {
                char msg[40];
                snprintf(msg, sizeof(msg), "Crafted %s!", recipes[app->craft_idx].name);
                app_toast(app, msg);
                notification_message(app->notif, &sequence_success);
                app->dirty = true;
            } else {
                app_toast(app, "Not enough shards");
                notification_message(app->notif, &sequence_error);
            }
        } else if(press && ev->key == InputKeyBack) {
            app->screen = ScreenMenu;
        }
        break;

    case ScreenBag:
        if(press && (ev->key == InputKeyLeft || ev->key == InputKeyRight)) {
            app->bag_tab ^= 1;
        } else if(dy && app->bag_tab == 0) {
            app->bag_idx = (uint8_t)((app->bag_idx + ItemCount + dy) % ItemCount);
        } else if(press && ev->key == InputKeyOk && app->bag_tab == 0) {
            uint8_t item = app->bag_idx;
            if(item != ItemTonic && item != ItemCrystal) {
                app_toast(app, "Use balls in battle");
            } else if(app->save.inv.items[item] == 0) {
                app_toast(app, "You have none");
            } else {
                /* Choose who to use it on, starting at the lead. */
                app->use_item_id = item;
                app->use_idx = app->save.lead;
                app->use_scroll = 0;
                list_move(&app->use_idx, &app->use_scroll, app->save.team_count, TEAM_ROWS, 0);
                app->toast_until = 0;
                app->screen = ScreenUseOn;
            }
        } else if(press && ev->key == InputKeyBack) {
            app->screen = ScreenMenu;
        }
        break;

    case ScreenSettings:
        if(dy) {
            app->settings_idx =
                (uint8_t)((app->settings_idx + SETTINGS_COUNT + dy) % SETTINGS_COUNT);
            app->confirm_reset = false;
        } else if(press && ev->key == InputKeyOk) {
            switch(app->settings_idx) {
            case 0:
                app->screen = ScreenBoard;
                break;
            case 1:
                app->help_scroll = 0;
                app->screen = ScreenHelp;
                break;
            case 2:
                if(!app->confirm_reset) {
                    app->confirm_reset = true;
                } else {
                    app->confirm_reset = false;
                    app_new_game(app);
                    app->nearby_count = 0;
                    app->nearby_idx = 0;
                    app->nearby_scroll = 0;
                    app->spotted_count = 0;
                    app->shards_this_run = 0;
                    app_save(app);
                    app->screen = ScreenIntro;
                }
                break;
            }
        } else if(press && ev->key == InputKeyBack) {
            app->confirm_reset = false;
            app->screen = ScreenMenu;
            if(app->dirty) app_save(app);
        }
        break;

    case ScreenBoard:
        board_input(app, ev, press, dy);
        break;

    case ScreenTerminal:
        terminal_input(app, ev, press);
        break;

    case ScreenUseOn:
        if(dy) {
            list_move(&app->use_idx, &app->use_scroll, app->save.team_count, TEAM_ROWS, dy);
        } else if(press && ev->key == InputKeyOk) {
            /* On success go back to the Bag (the toast shows there); on a
             * refusal (already healthy, max level) stay so another can be picked. */
            if(use_item_on(app, app->use_item_id, &app->save.team[app->use_idx]))
                app->screen = ScreenBag;
        } else if(press && ev->key == InputKeyBack) {
            app->screen = ScreenBag;
        }
        break;

    case ScreenCalibrate:
        if(press && (ev->key == InputKeyBack || (ev->key == InputKeyOk && calibrate_done(app)))) {
            calibrate_stop(app);
            app->screen = ScreenBoard;
        }
        break;

    case ScreenHelp:
        if(dy) {
            int n = (int)app->help_scroll + dy;
            if(n < 0) n = 0;
            if(n > (int)(HELP_COUNT - HELP_ROWS)) n = HELP_COUNT - HELP_ROWS;
            app->help_scroll = (uint8_t)n;
        } else if(press && (ev->key == InputKeyBack || ev->key == InputKeyOk)) {
            app->screen = ScreenSettings;
        }
        break;
    }
}

/* ------------------------------------------------------------------------- */
/* Periodic work                                                             */
/* ------------------------------------------------------------------------- */

static void app_tick(App* app) {
    uint32_t now = furi_get_tick();

    ScanEvent ev;
    bool debug = app->screen == ScreenTerminal || app->screen == ScreenCalibrate;
    for(int i = 0; i < 8 && scanner_poll(app->scanner, &ev); i++) {
        if(debug) continue; /* counted in the scanner stats only */
        if(ev.type == ScanEventAp)
            handle_ap(app, &ev);
        else
            handle_bt(app, &ev);
    }

    if(app->screen == ScreenCalibrate) calibrate_tick(app);

    bool in_battle = app->screen == ScreenBattle || app->screen == ScreenBallPick;
    if(now - app->last_regen >= furi_ms_to_ticks(REGEN_MS)) {
        app->last_regen = now;
        if(!in_battle) {
            for(uint8_t i = 0; i < app->save.team_count; i++) {
                Echoid* e = &app->save.team[i];
                EchoStats s;
                echo_stats(e, &s);
                if(e->hp < s.max_hp) e->hp++;
            }
        }
    }

    if(app->dirty && !in_battle && now - app->last_save >= furi_ms_to_ticks(AUTOSAVE_MS)) {
        app_save(app);
    }
}

/* ------------------------------------------------------------------------- */
/* Entry point                                                               */
/* ------------------------------------------------------------------------- */

int32_t echoids_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->battle_nearby = -1;
    game_rng_seed(furi_hal_random_get());

    if(save_load(&app->save)) {
        app->screen = ScreenMenu;
    } else {
        app_new_game(app);
        app_save(app);
        app->screen = ScreenIntro;
    }
    app->last_regen = furi_get_tick();
    app->last_save = furi_get_tick();

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->scanner = scanner_alloc();
    app->notif = furi_record_open(RECORD_NOTIFICATION);
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    app->running = true;
    while(app->running) {
        InputEvent ev;
        bool got = furi_message_queue_get(app->queue, &ev, 50) == FuriStatusOk;
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        if(got) handle_input(app, &ev);
        app_tick(app);
        furi_mutex_release(app->mutex);
        view_port_update(app->view_port);
    }

    scanner_stop(app->scanner);
    app_save(app);

    view_port_enabled_set(app->view_port, false);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    scanner_free(app->scanner);
    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
