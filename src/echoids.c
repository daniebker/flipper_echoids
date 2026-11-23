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
#define NEARBY_ROWS      4
#define CRAFT_ROWS       3

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
    uint8_t settings_idx;
    bool confirm_reset;
    uint8_t help_scroll;

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
    memset(&app->save, 0, sizeof(app->save));
    app->save.magic = SAVE_MAGIC;
    app->save.version = SAVE_VERSION;
    app->save.source = ScanSourceBoard;
    app->save.uart = ScanUartUsart;
    app->save.bt_enabled = 1;

    /* The starter is born from this Flipper's own Bluetooth MAC. */
    const uint8_t* mac = furi_hal_version_get_ble_mac();
    const char* name = furi_hal_version_get_name_ptr();
    echo_init(&app->save.team[0], mac, 1, name ? name : "Flipper");
    app->save.team_count = 1;
    app->save.lead = 0;
    app->dirty = true;
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

static void draw_collect(App* app, Canvas* canvas) {
    char right[24];
    snprintf(right, sizeof(right), "Shards %lu", (unsigned long)inventory_shard_total(&app->save.inv));
    const char* title = "Idle";
    switch(scanner_phase(app->scanner)) {
    case ScanPhaseWifi:
        title = "WiFi scan";
        break;
    case ScanPhaseBt:
        title = "BT scan";
        break;
    default:
        break;
    }
    draw_title(canvas, title, right);

    if(app->nearby_count == 0) {
        const char* err = scanner_error(app->scanner);
        uint32_t waited = furi_get_tick() - app->scan_started;
        if(err) {
            canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignCenter, err);
        } else if(
            !scanner_board_alive(app->scanner) &&
            waited > furi_ms_to_ticks(BOARD_TIMEOUT_MS)) {
            canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignCenter, "No data from board");
            canvas_draw_str_aligned(canvas, 64, 37, AlignCenter, AlignCenter, "Check Marauder FW/UART");
            canvas_draw_str_aligned(canvas, 64, 47, AlignCenter, AlignCenter, "or use Demo in Settings");
        } else {
            canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "Listening for echoes...");
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
        snprintf(buf, sizeof(buf), "%s%.9s", nearby_state_mark(n->state), n->ssid);
        canvas_draw_str(canvas, 2, y, buf);
        snprintf(buf, sizeof(buf), "L%u", echo_wild_level(n->mac, app_lead(app)->level));
        canvas_draw_str(canvas, 60, y, buf);
        canvas_draw_str(canvas, 78, y, echo_type_short(g.type));
        snprintf(buf, sizeof(buf), "%d", n->rssi);
        canvas_draw_str_aligned(canvas, 121, y, AlignRight, AlignBottom, buf);
    }
    canvas_set_color(canvas, ColorBlack);
    if(app->nearby_count > NEARBY_ROWS) {
        elements_scrollbar_pos(canvas, 127, 14, 48, app->nearby_idx, app->nearby_count);
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

static void draw_team(App* app, Canvas* canvas) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%u/%u", app->save.team_count, MAX_TEAM);
    draw_title(canvas, "Team", buf);
    for(uint8_t row = 0; row < TEAM_ROWS; row++) {
        uint8_t i = app->team_scroll + row;
        if(i >= app->save.team_count) break;
        Echoid* e = &app->save.team[i];
        EchoGenes g;
        echo_genes(e->mac, &g);
        EchoStats s;
        echo_stats(e, &s);
        int y = 22 + row * 10;
        draw_row(canvas, y, i == app->team_idx);
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
        elements_scrollbar_pos(canvas, 127, 14, 48, app->team_idx, app->save.team_count);
    }
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

static void draw_settings(App* app, Canvas* canvas) {
    draw_title(canvas, "Settings", NULL);
    const char* lines[5];
    char l0[24], l1[24], l2[24];
    snprintf(l0, sizeof(l0), "Source: %s", app->save.source == ScanSourceDemo ? "Demo" : "ESP32 board");
    snprintf(l1, sizeof(l1), "UART: %s", app->save.uart == ScanUartLpuart ? "15/16 LPUART" : "13/14 USART");
    snprintf(l2, sizeof(l2), "Bluetooth: %s", app->save.bt_enabled ? "On" : "Off");
    lines[0] = l0;
    lines[1] = l1;
    lines[2] = l2;
    lines[3] = "How to play";
    lines[4] = app->confirm_reset ? "Reset? OK again!" : "Reset game";
    for(uint8_t i = 0; i < 5; i++) {
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

static void collect_start(App* app) {
    for(uint8_t i = 0; i < app->nearby_count; i++) {
        if(app->nearby[i].state == NearbyWild && app_owns(app, app->nearby[i].mac))
            app->nearby[i].state = NearbyOwned;
    }
    app->scan_started = furi_get_tick();
    if(!scanner_start(
           app->scanner,
           (ScanSource)app->save.source,
           (ScanUart)app->save.uart,
           app->save.bt_enabled)) {
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

static void use_item(App* app, uint8_t item) {
    Inventory* inv = &app->save.inv;
    Echoid* lead = app_lead(app);
    EchoStats s;
    echo_stats(lead, &s);
    char msg[40];
    if(inv->items[item] == 0) {
        app_toast(app, "You have none");
        return;
    }
    if(item == ItemTonic) {
        if(lead->hp >= s.max_hp) {
            app_toast(app, "Lead is already healthy");
            return;
        }
        lead->hp = s.max_hp;
        inv->items[item]--;
        app_toast(app, "Lead fully healed!");
    } else if(item == ItemCrystal) {
        if(lead->level >= MAX_LEVEL) {
            app_toast(app, "Lead is max level");
            return;
        }
        inv->items[item]--;
        uint16_t xp = echo_xp_to_next(lead->level) / 2;
        uint8_t lv = echo_add_xp(lead, xp);
        if(lv)
            snprintf(msg, sizeof(msg), "+%u XP! Now Lv%u", xp, lead->level);
        else
            snprintf(msg, sizeof(msg), "+%u XP", xp);
        app_toast(app, msg);
    } else {
        app_toast(app, "Use balls in battle");
        return;
    }
    notification_message(app->notif, &sequence_success);
    app->dirty = true;
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
            use_item(app, app->bag_idx);
        } else if(press && ev->key == InputKeyBack) {
            app->screen = ScreenMenu;
        }
        break;

    case ScreenSettings:
        if(dy) {
            app->settings_idx = (uint8_t)((app->settings_idx + 5 + dy) % 5);
            app->confirm_reset = false;
        } else if(press && ev->key == InputKeyOk) {
            switch(app->settings_idx) {
            case 0:
                app->save.source = app->save.source == ScanSourceDemo ? ScanSourceBoard :
                                                                         ScanSourceDemo;
                break;
            case 1:
                app->save.uart = app->save.uart == ScanUartUsart ? ScanUartLpuart : ScanUartUsart;
                break;
            case 2:
                app->save.bt_enabled = !app->save.bt_enabled;
                break;
            case 3:
                app->help_scroll = 0;
                app->screen = ScreenHelp;
                break;
            case 4:
                if(!app->confirm_reset) {
                    app->confirm_reset = true;
                } else {
                    app->confirm_reset = false;
                    app_new_game(app);
                    app->nearby_count = 0;
                    app->nearby_idx = 0;
                    app->nearby_scroll = 0;
                    app_save(app);
                    app->screen = ScreenIntro;
                }
                break;
            }
            if(app->settings_idx <= 2) app->dirty = true;
        } else if(press && ev->key == InputKeyBack) {
            app->confirm_reset = false;
            app->screen = ScreenMenu;
            if(app->dirty) app_save(app);
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
    for(int i = 0; i < 8 && scanner_poll(app->scanner, &ev); i++) {
        if(ev.type == ScanEventAp)
            handle_ap(app, &ev);
        else
            handle_bt(app, &ev);
    }

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
