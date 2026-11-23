/*
 * Echoids - a creature collecting game for the Flipper Zero.
 *
 * WiFi access points are wild echoids that can be battled and captured.
 * Bluetooth devices drop shards which are crafted into echo balls and items.
 * Everything is derived deterministically from MAC addresses.
 */
#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_rtc.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/text_input.h>
#include <input/input.h>
#include <notification/notification_messages.h>

#include <stdio.h>
#include <string.h>

#include "game.h"
#include "pac_store.h"
#include "session.h"

#define BOARD_TITLE        "Board setup"
#include "save.h"
#include "scanner.h"

#define TOAST_MS         2500
#define BOARD_TIMEOUT_MS 6000
#define TEAM_ROWS        5
#define NEARBY_ROWS      3
#define CRAFT_ROWS       3
#define RESULT_ROWS        5
#define BAG_ROWS           5

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
    ScreenSwap, /* pick a team member to swap out for a PAC echoid */
    ScreenMovePick, /* battle: choose one of the active echoid's moves */
    ScreenBattleSwap, /* battle: choose a team member to send in */
    ScreenBattleResult, /* after a battle: XP, level-ups and new moves per echoid */
    ScreenJackIn, /* the board is connecting to a network */
    ScreenDungeon,
} Screen;

typedef struct {
    FuriMutex* mutex;
    FuriMessageQueue* queue;
    ViewPort* view_port;
    Gui* gui;
    NotificationApp* notif;
    Scanner* scanner;

    /* The game itself: every rule lives in session.c. This file only draws
     * it and turns button presses into session actions. */
    Session s;
    bool running;
    uint32_t scan_started;

    Screen screen;
    uint8_t menu_idx;
    uint8_t menu_scroll;

    uint8_t nearby_idx;
    uint8_t nearby_scroll;
    bool collect_menu; /* Battle / Jack in popup on Collect */
    uint8_t collect_menu_idx;

    /* Jacking in and dungeons */
    uint8_t jack_index; /* nearby echoid whose network we're joining */
    bool dungeon_battle; /* the battle on screen is a dungeon fight */
    bool confirm_jack_out;

    uint8_t battle_menu;
    uint8_t move_idx;
    uint8_t bswap_idx;
    uint8_t bswap_scroll;
    uint8_t result_scroll;
    uint8_t detail_page; /* details screen page: DetailStats, DetailMoves, DetailRating */
    uint8_t ball_idx;

    /* Team screen: tab 0 = team, 1 = PAC. Indexes are positions in that list. */
    uint8_t team_tab;
    uint8_t team_idx;
    uint8_t team_scroll;
    uint32_t pac_idx;
    uint32_t pac_scroll;
    bool detail_menu; /* actions popup open on the detail screen */
    uint8_t detail_menu_idx;
    uint8_t swap_idx; /* team member to swap out for the PAC echoid at pac_idx */
    uint8_t swap_scroll;

    /* PAC echoids live on the SD card: the visible page and the one being
     * viewed are cached here and refreshed from the main loop, never from
     * the draw callback. */
    Echoid pac_page[TEAM_ROWS];
    uint8_t pac_page_n;
    uint32_t pac_page_start;
    bool pac_page_valid;
    Echoid pac_view;
    uint8_t craft_idx;
    uint8_t craft_scroll;
    uint8_t bag_tab;
    uint8_t bag_idx;
    uint8_t bag_scroll;
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

    char toast[40];
    uint32_t toast_until;
    bool toast_important; /* a "+1 shard" popup: don't cover it with a recharge note */
} App;

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static void app_toast(App* app, const char* msg) {
    strncpy(app->toast, msg, sizeof(app->toast) - 1);
    app->toast[sizeof(app->toast) - 1] = '\0';
    app->toast_until = furi_get_tick() + furi_ms_to_ticks(TOAST_MS);
    app->toast_important = false;
}

static bool app_toast_showing(App* app) {
    return app->toast[0] && (int32_t)(app->toast_until - furi_get_tick()) > 0;
}

/* Array indexes of the team (party=true) or the PAC. */
static void pac_invalidate(App* app) {
    app->pac_page_valid = false;
}

/* Loads the visible PAC page if the scroll position changed. Main loop only. */
static void pac_refresh(App* app) {
    if(app->pac_page_valid && app->pac_page_start == app->pac_scroll) return;
    app->pac_page_start = app->pac_scroll;
    app->pac_page_n = 0;
    if(app->pac_scroll < app->s.save.pac_count) {
        uint32_t left = app->s.save.pac_count - app->pac_scroll;
        uint8_t want = left < TEAM_ROWS ? (uint8_t)left : TEAM_ROWS;
        app->pac_page_n = pac_read_page(app->pac_scroll, app->pac_page, want);
    }
    app->pac_page_valid = true;
}

static void app_scan_config(App* app, ScanConfig* cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->uart = app->s.save.uart % ScanUartCount;
    cfg->baud_idx = app->s.save.baud_idx % scanner_baud_count;
    cfg->power = app->s.save.power % ScanPowerCount;
    cfg->boot_idx = app->s.save.boot_idx % scanner_boot_count;
    cfg->eol = app->s.save.eol & 1;
    cfg->wifi_cmd = app->s.save.wifi_cmd % scanner_wifi_cmd_count;
    cfg->bt_cmd = app->s.save.bt_cmd % scanner_bt_cmd_count;
    cfg->bt_enabled = app->s.save.bt_enabled;
    cfg->log_sd = app->s.save.log_sd;
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

enum {
    DetailStats,
    DetailMoves,
    DetailRating,
    DetailPageCount,
};

/* 5x5 star, filled or outlined; y is the top row. */
static void draw_star(Canvas* canvas, int x, int y, bool filled) {
    static const uint8_t full[5] = {0x04, 0x0E, 0x1F, 0x0E, 0x0A};
    static const uint8_t outline[5] = {0x04, 0x0A, 0x11, 0x0A, 0x0A};
    const uint8_t* rows = filled ? full : outline;
    for(int r = 0; r < 5; r++)
        for(int c = 0; c < 5; c++)
            if(rows[r] & (0x10 >> c)) canvas_draw_dot(canvas, x + c, y + r);
}

/* Three stars, the first `stars` filled; y is the text baseline it lines up with. */
static void draw_stars(Canvas* canvas, int x, int baseline, uint8_t stars) {
    for(uint8_t i = 0; i < 3; i++)
        draw_star(canvas, x + i * 6, baseline - 6, i < stars);
}

/* Rarity badge: a letter (C U R E L); Epic and Legendary get a box so they
 * stand out. y is the text baseline; uses the current color. */
static void draw_rarity(Canvas* canvas, int x, int y, uint8_t rarity) {
    char s[2] = {echo_rarity_letter(rarity), '\0'};
    canvas_draw_str(canvas, x, y, s);
    if(rarity >= RarityEpic) canvas_draw_frame(canvas, x - 2, y - 8, 9, 10);
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

/* Highlight bar for a list row; y is the text baseline, width in pixels. */
static void draw_row_w(Canvas* canvas, int y, bool selected, int width) {
    if(selected) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, y - 8, width, 10);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_set_color(canvas, ColorBlack);
    }
}

static void draw_row(Canvas* canvas, int y, bool selected) {
    draw_row_w(canvas, y, selected, 123); /* leave room for the scrollbar */
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

enum {
    MenuCollect,
    MenuTeam,
    MenuPac,
    MenuCraft,
    MenuBag,
    MenuSettings,
    MenuDungeon,
    MenuJackOut,
};
static const char* const menu_names[] =
    {"Collect", "Team", "PAC", "Craft", "Bag", "Settings", "Dungeon", "Jack out"};
/* What the menu offers: in a dungeon, only the dungeon, team and bag. */
static const uint8_t menu_normal[] = {MenuCollect, MenuTeam, MenuPac, MenuCraft, MenuBag, MenuSettings};
static const uint8_t menu_dungeon[] = {MenuDungeon, MenuTeam, MenuBag, MenuJackOut};
#define MENU_ROWS 5

static const uint8_t* menu_list(App* app, uint8_t* count) {
    if(session_in_dungeon(&app->s)) {
        *count = sizeof(menu_dungeon);
        return menu_dungeon;
    }
    *count = sizeof(menu_normal);
    return menu_normal;
}

static void draw_intro(App* app, Canvas* canvas) {
    Echoid* e = session_lead(&app->s);
    EchoGenes g;
    echo_genes(e->mac, &g);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Your echo awakens!");
    draw_sprite(canvas, e->mac, 2, 16, 2);
    canvas_set_font(canvas, FontSecondary);
    char buf[32];
    char name[ECHO_NAME_LEN];
    echo_name(e->mac, name, sizeof(name));
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 40, 23, name);
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "%s, %s", echo_species_name(e->mac), echo_type_name(g.type));
    canvas_draw_str(canvas, 40, 32, buf);
    snprintf(buf, sizeof(buf), "%s, Level 1", echo_rarity_name(g.rarity));
    canvas_draw_str(canvas, 40, 41, buf);
    canvas_draw_str(canvas, 40, 50, "Born from your MAC");
    elements_button_center(canvas, "Start");
}

static void draw_menu(App* app, Canvas* canvas) {
    Echoid* e = session_lead(&app->s);
    EchoStats s;
    echo_stats(e, &s);
    uint8_t count;
    const uint8_t* items = menu_list(app, &count);
    draw_title(canvas, session_in_dungeon(&app->s) ? "Jacked in" : "Echoids", NULL);
    for(uint8_t row = 0; row < MENU_ROWS; row++) {
        uint8_t i = app->menu_scroll + row;
        if(i >= count) break;
        int y = 23 + row * 10;
        /* Stop short of the scrollbar and the lead echoid panel (divider at x=84). */
        draw_row_w(canvas, y, i == app->menu_idx, 79);
        canvas_draw_str(
            canvas,
            4,
            y,
            items[i] == MenuJackOut && app->confirm_jack_out ? "Sure? OK!" : menu_names[items[i]]);
        if(items[i] == MenuPac) {
            /* How many echoids are in storage */
            char n[12];
            snprintf(n, sizeof(n), "%lu", (unsigned long)app->s.save.pac_count);
            canvas_draw_str_aligned(canvas, 76, y, AlignRight, AlignBottom, n);
        }
    }
    canvas_set_color(canvas, ColorBlack);
    elements_scrollbar_pos(canvas, 82, 14, 49, app->menu_idx, count);
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
    snprintf(num, sizeof(num), "%u", app->s.shards_this_run);
    int x = 127 - (int)canvas_string_width(canvas, num);
    canvas_draw_str(canvas, x, 10, num);
    x -= 9;
    draw_shard_icon(canvas, x, 3);
    x -= 6;
    snprintf(num, sizeof(num), "%u", app->s.spotted_count);
    x -= (int)canvas_string_width(canvas, num);
    canvas_draw_str(canvas, x, 10, num);
    x -= 9;
    draw_ball_icon(canvas, x, 3);

    if(app->s.nearby_count == 0) {
        const char* err = scanner_error(app->scanner);
        uint32_t waited = furi_get_tick() - app->scan_started;
        if(err) {
            canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignCenter, err);
            canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, "Settings > Board setup");
        } else if(scanner_booting(app->scanner)) {
            canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "Powering up board...");
        } else if(app->s.asleep_count > 0) {
            /* Access points were heard, but their echoids aren't out at this hour. */
            char info[40];
            snprintf(
                info,
                sizeof(info),
                "%u echo%s asleep at %02u:00",
                app->s.asleep_count,
                app->s.asleep_count == 1 ? "id" : "ids",
                app->s.hour);
            canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignCenter, info);
            canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignCenter, "Come back at another hour!");
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
        if(i >= app->s.nearby_count) break;
        Nearby* n = &app->s.nearby[i];
        Echoid wild;
        session_nearby_echoid(&app->s, i, &wild);
        int y = 23 + row * 10;
        draw_row(canvas, y, i == app->nearby_idx);
        EchoGenes g;
        echo_genes(n->mac, &g);
        char buf[24];
        char name[ECHO_NAME_LEN];
        echo_name(n->mac, name, sizeof(name));
        snprintf(buf, sizeof(buf), "%s%s", nearby_state_mark(n->state), name);
        canvas_draw_str(canvas, 2, y, buf);
        snprintf(buf, sizeof(buf), "L%u", wild.level);
        canvas_draw_str(canvas, 58, y, buf);
        canvas_draw_str(canvas, 76, y, echo_type_short(g.type));
        draw_rarity(canvas, 95, y, g.rarity);
        snprintf(buf, sizeof(buf), "%d", n->rssi);
        canvas_draw_str_aligned(canvas, 121, y, AlignRight, AlignBottom, buf);
    }
    canvas_set_color(canvas, ColorBlack);
    if(app->s.nearby_count > NEARBY_ROWS) {
        elements_scrollbar_pos(canvas, 127, 14, 31, app->nearby_idx, app->s.nearby_count);
    }

    /* Stats panel for the selected echoid, at the level it would battle at. */
    if(app->s.nearby_count > 0 && app->nearby_idx < app->s.nearby_count) {
        Nearby* n = &app->s.nearby[app->nearby_idx];
        EchoGenes g;
        echo_genes(n->mac, &g);
        Echoid wild;
        session_nearby_echoid(&app->s, app->nearby_idx, &wild);
        EchoStats st;
        echo_stats(&wild, &st);
        char buf[40];
        canvas_draw_line(canvas, 0, 45, 127, 45);
        snprintf(buf, sizeof(buf), "%s %s", echo_species_name(n->mac), echo_rarity_name(g.rarity));
        canvas_draw_str(canvas, 2, 54, buf);
        /* Star rating of its random stat rolls */
        EchoRating rating;
        echo_rating(n->mac, &rating);
        draw_stars(canvas, 110, 54, rating.overall);
        snprintf(
            buf, sizeof(buf), "HP%u ATK%u DEF%u SPD%u", st.max_hp, st.atk, st.def, st.spd);
        canvas_draw_str(canvas, 2, 63, buf);
    }
    if(app->collect_menu) {
        static const char* const opts[] = {"Battle!", "Jack in"};
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 70, 14, 56, 28);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_rframe(canvas, 70, 14, 56, 28, 2);
        for(uint8_t i = 0; i < 2; i++) {
            int y = 26 + i * 11;
            if(i == app->collect_menu_idx) {
                canvas_draw_box(canvas, 72, y - 9, 52, 11);
                canvas_set_color(canvas, ColorWhite);
            }
            canvas_draw_str(canvas, 76, y, opts[i]);
            canvas_set_color(canvas, ColorBlack);
        }
    }
    draw_toast(app, canvas);
}

static void draw_jack_in(App* app, Canvas* canvas) {
    const Nearby* n = &app->s.nearby[app->jack_index % SESSION_NEARBY_MAX];
    draw_title(canvas, "Jacking in", NULL);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, n->ssid);
    const char* what = "Waking the board...";
    if(scanner_booting(app->scanner)) {
        what = "Powering up board...";
    } else if(scanner_join_state(app->scanner) == ScanJoinConnecting) {
        what = "Connecting...";
    } else if(scanner_join_state(app->scanner) == ScanJoinFinding) {
        what = "Finding the network...";
    }
    canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, what);
    /* A little activity bar */
    int x = (int)((furi_get_tick() / 60) % 100);
    canvas_draw_frame(canvas, 14, 46, 100, 5);
    canvas_draw_box(canvas, 14 + (x < 80 ? x : 80), 47, 20, 3);
    canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "Back: cancel");
    draw_toast(app, canvas);
}

static void draw_dungeon(App* app, Canvas* canvas) {
    const Dungeon* d = &app->s.dungeon;
    char buf[40];
    snprintf(buf, sizeof(buf), "%u/%u", d->room + 1, d->map.rooms);
    draw_title(canvas, d->name, buf);

    /* The path: one box per room, done ones filled in, the current one framed twice. */
    static const char kinds[] = {'x', '$', 'z', 'B'}; /* fight, cache, rest, boss */
    int w = 13, x0 = 64 - (d->map.rooms * w) / 2;
    for(uint8_t r = 0; r < d->map.rooms; r++) {
        int x = x0 + r * w;
        bool done = r < d->room || (r == d->room && d->room_done);
        char k[2] = {kinds[d->map.kind[r] % 4], '\0'};
        if(done) {
            canvas_draw_box(canvas, x, 15, 11, 11);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_frame(canvas, x, 15, 11, 11);
        }
        if(r == d->room) canvas_draw_frame(canvas, x - 1, 14, 13, 13);
        canvas_draw_str_aligned(canvas, x + 6, 21, AlignCenter, AlignCenter, k);
        canvas_set_color(canvas, ColorBlack);
    }

    /* This room */
    uint8_t kind = d->map.kind[d->room];
    Echoid foe;
    const char* hint;
    if(d->cleared) {
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, "The boss is beaten!");
        hint = "Jack out from the menu";
    } else if(d->room_done) {
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, "Room cleared");
        hint = "OK: go deeper";
    } else if(session_dungeon_foe(&app->s, d->room, &foe)) {
        draw_sprite(canvas, foe.mac, 2, 29, 1);
        char name[ECHO_NAME_LEN];
        echo_name(foe.mac, name, sizeof(name));
        EchoGenes g;
        echo_genes(foe.mac, &g);
        snprintf(buf, sizeof(buf), "%s%s L%u", kind == RoomBoss ? "BOSS " : "", name, foe.level);
        canvas_draw_str(canvas, 22, 36, buf);
        snprintf(buf, sizeof(buf), "%s %s", echo_type_name(g.type), echo_rarity_name(g.rarity));
        canvas_draw_str(canvas, 22, 45, buf);
        hint = kind == RoomBoss ? "OK: face the boss" : "OK: fight";
    } else {
        canvas_draw_str_aligned(
            canvas, 64, 38, AlignCenter, AlignCenter, kind == RoomCache ? "A cache of shards" : "A place to rest");
        hint = kind == RoomCache ? "OK: open it" : "OK: rest";
    }
    canvas_draw_line(canvas, 0, 52, 127, 52);
    canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, hint);
    draw_toast(app, canvas);
}

static void draw_battle(App* app, Canvas* canvas) {
    Battle* b = &app->s.battle;
    char buf[40];
    EchoStats sw, sm;
    echo_stats(&b->wild, &sw);
    echo_stats(b->mine, &sm);

    canvas_set_font(canvas, FontSecondary);
    char name[ECHO_NAME_LEN];
    echo_name(b->wild.mac, name, sizeof(name));
    snprintf(buf, sizeof(buf), "%s L%u", name, b->wild.level);
    canvas_draw_str(canvas, 0, 8, buf);
    EchoGenes gw;
    echo_genes(b->wild.mac, &gw);
    snprintf(
        buf,
        sizeof(buf),
        "%s %s %c",
        echo_species_name(b->wild.mac),
        echo_type_short(gw.type),
        echo_rarity_letter(gw.rarity));
    canvas_draw_str(canvas, 0, 17, buf);
    draw_hp_bar(canvas, 0, 19, 60, b->wild.hp, sw.max_hp);
    draw_sprite(canvas, b->wild.mac, 94, 0, 2);

    echo_name(b->mine->mac, name, sizeof(name));
    snprintf(buf, sizeof(buf), "%s L%u", name, b->mine->level);
    canvas_draw_str(canvas, 0, 32, buf);
    draw_hp_bar(canvas, 0, 34, 50, b->mine->hp, sm.max_hp);
    snprintf(buf, sizeof(buf), "%u/%u", b->mine->hp, sm.max_hp);
    canvas_draw_str(canvas, 53, 39, buf);

    canvas_draw_str(canvas, 0, 47, b->log[0]);
    canvas_draw_str(canvas, 0, 55, b->log[1]);

    if(b->result == BattleOngoing) {
        static const char* const opts[] = {"Hit", "Swap", "Ball", "Run"};
        for(int i = 0; i < 4; i++) {
            int x = i * 32;
            int w = 32;
            if(i == app->battle_menu) {
                canvas_draw_box(canvas, x, 56, w, 8);
                canvas_set_color(canvas, ColorWhite);
            }
            canvas_draw_str_aligned(canvas, x + w / 2, 63, AlignCenter, AlignBottom, opts[i]);
            canvas_set_color(canvas, ColorBlack);
        }
    } else {
        canvas_draw_box(canvas, 0, 56, 128, 8);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, "OK: continue");
        canvas_set_color(canvas, ColorBlack);
    }
    draw_toast(app, canvas);
}

static void draw_move_pick(App* app, Canvas* canvas) {
    draw_battle(app, canvas);
    Move moves[MAX_MOVES];
    uint8_t n = echo_moves(app->s.battle.mine, moves);
    EchoGenes gw;
    echo_genes(app->s.battle.wild.mac, &gw);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 6, 6, 116, 48);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, 6, 6, 116, 48, 3);
    canvas_set_font(canvas, FontSecondary);
    for(uint8_t i = 0; i < n; i++) {
        int y = 17 + i * 10;
        char buf[16];
        if(i == app->move_idx) {
            canvas_draw_box(canvas, 8, y - 8, 112, 10);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, 10, y, moves[i].name);
        /* How it fares against this wild echoid */
        uint8_t mult = move_mult(&moves[i], gw.type);
        canvas_draw_str(canvas, 72, y, mult > 100 ? "x2" : (mult < 100 ? "x.5" : ""));
        canvas_draw_str(canvas, 86, y, move_type_short(&moves[i]));
        snprintf(buf, sizeof(buf), "%u", moves[i].power);
        canvas_draw_str_aligned(canvas, 118, y, AlignRight, AlignBottom, buf);
        canvas_set_color(canvas, ColorBlack);
    }
}

static void draw_battle_result(App* app, Canvas* canvas) {
    char right[24];
    snprintf(right, sizeof(right), "+%lu XP", (unsigned long)app->s.battle.xp_gained);
    draw_title(canvas, "Results", right);
    for(uint8_t row = 0; row < RESULT_ROWS; row++) {
        uint8_t i = app->result_scroll + row;
        if(i >= app->s.result_count) break;
        canvas_draw_str(canvas, 2, 22 + row * 10, app->s.results[i]);
    }
    if(app->s.result_count > RESULT_ROWS)
        elements_scrollbar_pos(
            canvas, 127, 14, 48, app->result_scroll, app->s.result_count - RESULT_ROWS + 1);
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
        snprintf(buf, sizeof(buf), "x%u", app->s.save.inv.items[i]);
        canvas_draw_str(canvas, 70, y, buf);
        snprintf(buf, sizeof(buf), "%u%%", echo_capture_chance(&app->s.battle.wild, i));
        canvas_draw_str_aligned(canvas, 109, y, AlignRight, AlignBottom, buf);
        canvas_set_color(canvas, ColorBlack);
    }
}

/* Draws a list of echoids. rows[] are the visible ones, starting at scroll. */
static void draw_roster(
    App* app,
    Canvas* canvas,
    const char* title,
    const char* right,
    const Echoid* rows,
    uint8_t nrows,
    uint32_t total,
    uint32_t sel,
    uint32_t scroll,
    int32_t lead_pos) {
    UNUSED(app);
    char buf[32];
    draw_title(canvas, title, right);
    if(total == 0) {
        canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignCenter, "Empty");
        return;
    }
    for(uint8_t row = 0; row < nrows && row < TEAM_ROWS; row++) {
        uint32_t pos = scroll + row;
        const Echoid* e = &rows[row];
        EchoGenes g;
        echo_genes(e->mac, &g);
        EchoStats s;
        echo_stats(e, &s);
        int y = 22 + row * 10;
        draw_row(canvas, y, pos == sel);
        char name[ECHO_NAME_LEN];
        echo_name(e->mac, name, sizeof(name));
        snprintf(buf, sizeof(buf), "%s%s", (int32_t)pos == lead_pos ? ">" : " ", name);
        canvas_draw_str(canvas, 2, y, buf);
        snprintf(buf, sizeof(buf), "L%u", e->level);
        canvas_draw_str(canvas, 62, y, buf);
        canvas_draw_str(canvas, 80, y, echo_type_short(g.type));
        draw_rarity(canvas, 98, y, g.rarity);
        canvas_draw_str_aligned(canvas, 121, y, AlignRight, AlignBottom, e->hp ? "" : "KO");
        if(e->hp) {
            /* tiny hp bar */
            int fill = (int)(12u * e->hp / (s.max_hp ? s.max_hp : 1));
            canvas_draw_frame(canvas, 106, y - 6, 14, 5);
            if(fill > 0) canvas_draw_box(canvas, 107, y - 5, fill, 3);
        }
    }
    canvas_set_color(canvas, ColorBlack);
    if(total > TEAM_ROWS) {
        elements_scrollbar_pos(canvas, 127, 14, 48, sel, total);
    }
}

/* The team as a roster list. */
static void draw_team_roster(App* app, Canvas* canvas, const char* title, const char* right, uint8_t sel, uint8_t scroll) {
    uint8_t n = app->s.save.team_count > scroll ? app->s.save.team_count - scroll : 0;
    draw_roster(
        app, canvas, title, right, &app->s.save.team[scroll], n, app->s.save.team_count, sel, scroll, app->s.save.lead);
}

static void draw_team(App* app, Canvas* canvas) {
    char buf[24];
    if(app->team_tab == 0) {
        snprintf(buf, sizeof(buf), "%u/%u  <>", app->s.save.team_count, PARTY_MAX);
        draw_team_roster(app, canvas, "Team", buf, app->team_idx, app->team_scroll);
    } else {
        snprintf(
            buf,
            sizeof(buf),
            "%lu/%lu  <>",
            (unsigned long)app->s.save.pac_count,
            (unsigned long)save_pac_capacity(&app->s.save));
        draw_roster(
            app,
            canvas,
            "Cold PAC",
            buf,
            app->pac_page,
            app->pac_page_valid ? app->pac_page_n : 0,
            app->s.save.pac_count,
            app->pac_idx,
            app->pac_scroll,
            -1);
    }
    draw_toast(app, canvas);
}

static void draw_battle_swap(App* app, Canvas* canvas) {
    uint8_t n = app->s.save.team_count > app->bswap_scroll ? app->s.save.team_count - app->bswap_scroll : 0;
    int32_t active = (int32_t)(app->s.battle.mine - app->s.save.team);
    /* ">" marks the echoid currently fighting. */
    draw_roster(
        app,
        canvas,
        app->s.must_switch ? "Send in who?" : "Switch to?",
        NULL,
        &app->s.save.team[app->bswap_scroll],
        n,
        app->s.save.team_count,
        app->bswap_idx,
        app->bswap_scroll,
        active);
    draw_toast(app, canvas);
}

static void draw_swap(App* app, Canvas* canvas) {
    draw_team_roster(app, canvas, "Swap out who?", NULL, app->swap_idx, app->swap_scroll);
    draw_toast(app, canvas);
}

static void draw_use_on(App* app, Canvas* canvas) {
    char buf[16];
    snprintf(buf, sizeof(buf), "x%u", app->s.save.inv.items[app->use_item_id]);
    draw_team_roster(
        app,
        canvas,
        session_item_use(app->use_item_id) == ItemUseHeal   ? "Heal who?" :
        session_item_use(app->use_item_id) == ItemUseRevive ? "Revive who?" :
                                                              "Crystal on?",
        buf,
        app->use_idx,
        app->use_scroll);
    draw_toast(app, canvas);
}

/* The echoid on the detail screen, its position and the list size. */
static const Echoid* detail_target(App* app, uint32_t* pos, uint32_t* n) {
    if(app->team_tab == 0) {
        *n = app->s.save.team_count;
        *pos = app->team_idx < *n ? app->team_idx : 0;
        return &app->s.save.team[*pos];
    }
    *n = app->s.save.pac_count;
    *pos = app->pac_idx;
    return &app->pac_view;
}

static const char* const detail_team_actions[] = {"Set as lead", "Send to PAC"};
static const char* const detail_pac_actions[] = {"Move to team"};

static void draw_detail(App* app, Canvas* canvas) {
    uint32_t pos, n;
    const Echoid* e = detail_target(app, &pos, &n);
    bool is_lead = app->team_tab == 0 && pos == app->s.save.lead;
    EchoGenes g;
    echo_genes(e->mac, &g);
    EchoStats s;
    echo_stats(e, &s);
    char buf[40];

    canvas_set_font(canvas, FontPrimary);
    char name[ECHO_NAME_LEN];
    echo_name(e->mac, name, sizeof(name));
    canvas_draw_str(canvas, 0, 9, name);
    canvas_set_font(canvas, FontSecondary);
    snprintf(
        buf,
        sizeof(buf),
        "%s%s %lu/%lu",
        is_lead ? "LEAD " : "",
        app->team_tab == 0 ? "Team" : "PAC",
        (unsigned long)(pos + 1),
        (unsigned long)n);
    canvas_draw_str_aligned(canvas, 127, 9, AlignRight, AlignBottom, buf);

    draw_sprite(canvas, e->mac, 0, 12, 2);
    EchoRating rating;
    echo_rating(e->mac, &rating);
    snprintf(buf, sizeof(buf), "%s %s", echo_type_name(g.type), echo_rarity_name(g.rarity));
    canvas_draw_str(canvas, 36, 19, buf);
    draw_stars(canvas, 110, 19, rating.overall);
    snprintf(buf, sizeof(buf), "Lv%u  XP %u/%u", e->level, e->xp, echo_xp_to_next(e->level));
    canvas_draw_str(canvas, 36, 28, buf);
    snprintf(buf, sizeof(buf), "HP %u/%u", e->hp, s.max_hp);
    canvas_draw_str(canvas, 36, 37, buf);
    snprintf(buf, sizeof(buf), "A%u D%u S%u", s.atk, s.def, s.spd);
    canvas_draw_str(canvas, 36, 46, buf);
    if(app->detail_page == DetailRating) {
        /* Rating page: how good each random stat roll is. */
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 35, 11, 93, 53);
        canvas_draw_box(canvas, 0, 46, 128, 18);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_str(canvas, 36, 19, rating.overall == 3 ? "PERFECT!" : "Rating");
        draw_stars(canvas, 110, 19, rating.overall);
        for(uint8_t i = 0; i < STAT_COUNT; i++) {
            int y = 28 + i * 9;
            canvas_draw_str(canvas, 36, y, stat_name(i));
            snprintf(buf, sizeof(buf), "%u/%u", rating.roll[i], rating.max[i]);
            canvas_draw_str(canvas, 60, y, buf);
            draw_stars(canvas, 110, y, rating.stars[i]);
        }
        snprintf(buf, sizeof(buf), "Stars %u/%u  ^v stats", rating.total_stars, STAT_COUNT * 3);
        canvas_draw_str(canvas, 0, 63, buf);
    } else if(app->detail_page == DetailMoves) {
        /* Moves page replaces the stats area. */
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 35, 11, 93, 53);
        canvas_draw_box(canvas, 0, 46, 128, 18);
        canvas_set_color(canvas, ColorBlack);
        Move moves[MAX_MOVES];
        uint8_t n = echo_moves(e, moves);
        for(uint8_t i = 0; i < n; i++) {
            int y = 19 + i * 9;
            canvas_draw_str(canvas, 36, y, moves[i].name);
            /* Power only: "Event Horizon" plus a type would not fit. */
            snprintf(buf, sizeof(buf), "%u", moves[i].power);
            canvas_draw_str_aligned(canvas, 127, y, AlignRight, AlignBottom, buf);
        }
        uint8_t at = 0;
        const Move* next = echo_next_move(e, &at);
        if(next)
            snprintf(buf, sizeof(buf), "Next: %s at Lv%u", next->name, at);
        else
            snprintf(buf, sizeof(buf), "Knows its final moves");
        canvas_draw_str(canvas, 0, 55, buf);
        /* The two opposed types this echoid can never learn moves of. */
        uint8_t cant[2];
        echo_unlearnable_types(g.type, cant);
        snprintf(
            buf, sizeof(buf), "Can't learn %s or %s", echo_type_name(cant[0]), echo_type_name(cant[1]));
        canvas_draw_str(canvas, 0, 63, buf);
    } else {
        /* Species and where it was found */
        if(e->origin[0])
            snprintf(buf, sizeof(buf), "%s from %.14s", echo_species_name(e->mac), e->origin);
        else
            snprintf(buf, sizeof(buf), "%s", echo_species_name(e->mac));
        canvas_draw_str(canvas, 0, 54, buf);
        canvas_draw_str(canvas, 0, 63, "^v more pages  OK actions");
    }

    if(app->detail_menu) {
        const char* const* opts = app->team_tab == 0 ? detail_team_actions : detail_pac_actions;
        uint8_t count = app->team_tab == 0 ? 2 : 1;
        int h = 6 + count * 10;
        int y0 = 63 - h;
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 56, y0, 72, h + 1);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_rframe(canvas, 56, y0, 72, h + 1, 2);
        for(uint8_t i = 0; i < count; i++) {
            int y = y0 + 12 + i * 10;
            if(i == app->detail_menu_idx) {
                canvas_draw_box(canvas, 58, y - 8, 68, 10);
                canvas_set_color(canvas, ColorWhite);
            }
            canvas_draw_str(canvas, 61, y, opts[i]);
            canvas_set_color(canvas, ColorBlack);
        }
    }
    draw_toast(app, canvas);
}

static void draw_craft(App* app, Canvas* canvas) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Shards %lu", (unsigned long)inventory_shard_total(&app->s.save.inv));
    draw_title(canvas, "Craft", buf);
    for(uint8_t row = 0; row < CRAFT_ROWS; row++) {
        uint8_t i = app->craft_scroll + row;
        if(i >= recipe_count) break;
        const Recipe* r = &recipes[i];
        int y = 22 + row * 10;
        draw_row(canvas, y, i == app->craft_idx);
        snprintf(buf, sizeof(buf), "%s%s", craft_can(&app->s.save.inv, i) ? "+" : " ", r->name);
        canvas_draw_str(canvas, 2, y, buf);
        snprintf(buf, sizeof(buf), "have %lu", (unsigned long)session_recipe_have(&app->s, i));
        canvas_draw_str_aligned(canvas, 121, y, AlignRight, AlignBottom, buf);
    }
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_line(canvas, 0, 46, 127, 46);
    canvas_draw_str(canvas, 2, 55, recipes[app->craft_idx].recipe);
    const char* status = "OK: craft";
    if(!craft_can(&app->s.save.inv, app->craft_idx)) status = "Not enough shards";
    canvas_draw_str(canvas, 2, 63, status);
    elements_scrollbar_pos(canvas, 127, 14, 31, app->craft_idx, recipe_count);
    draw_toast(app, canvas);
}

static void draw_bag(App* app, Canvas* canvas) {
    char buf[32];
    draw_title(canvas, app->bag_tab == 0 ? "Bag: Items" : "Bag: Shards", "<  >");
    if(app->bag_tab == 0) {
        for(uint8_t row = 0; row < BAG_ROWS; row++) {
            uint8_t i = app->bag_scroll + row;
            if(i >= ItemCount) break;
            int y = 22 + row * 10;
            draw_row(canvas, y, i == app->bag_idx);
            canvas_draw_str(canvas, 2, y, item_name(i));
            snprintf(buf, sizeof(buf), "x%u", app->s.save.inv.items[i]);
            canvas_draw_str_aligned(canvas, 121, y, AlignRight, AlignBottom, buf);
        }
        canvas_set_color(canvas, ColorBlack);
        elements_scrollbar_pos(canvas, 127, 14, 48, app->bag_idx, ItemCount);
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
                app->s.save.inv.shards[t][0],
                app->s.save.inv.shards[t][1],
                app->s.save.inv.shards[t][2]);
            canvas_draw_str(canvas, x, y, buf);
        }
    }
    draw_toast(app, canvas);
}

#define SETTINGS_COUNT 3

static void draw_settings(App* app, Canvas* canvas) {
    draw_title(canvas, "Settings", NULL);
    const char* lines[SETTINGS_COUNT] = {
        BOARD_TITLE,
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
    "Each echoid only comes out",
    "at certain hours of the",
    "day: come back at another",
    "time to meet others.",
    "Pick one and press OK to",
    "battle it.",
    "",
    "Bluetooth devices drop one",
    "shard each (MAC decides",
    "element and tier). Then",
    "they recharge: Dull 15m,",
    "Bright 1h, Prismatic 4h.",
    "",
    "Craft Echo Balls from",
    "shards. Weaken a wild echoid",
    "then throw a ball to catch.",
    "",
    "Types: each beats the next",
    "Tide>Ember>Flora>Stone>",
    "Spark>Gale>Pulse>Void>Tide",
    "Hit: pick one of up to 4",
    "moves. New moves come with",
    "levels (max Lv100). Every",
    "move has a type and uses",
    "the type chart. Swap sends",
    "in another team member.",
    "",
    "Team holds 6 echoids; new",
    "catches beyond that go to",
    "the PAC. Team <> PAC tabs.",
    "Craft PACs for +10 room,",
    "as many as you like. The",
    "PAC heals echoids.",
    "",
    "Win or catch to gain XP;",
    "everyone who fought shares",
    "it evenly.",
    "HP regenerates slowly, or",
    "use Tonics: +20, Super +50,",
    "Mega +100, Max = full HP.",
    "Fainted echoids don't heal:",
    "use a Revive Crystal first.",
    "* owned  x already fought",
    "Stars rate an echoid's",
    "random stats: 3 stars on",
    "every stat = perfect.",
    "Rarity: C common, U uncom-",
    "mon, R rare, [E] epic,",
    "[L] legendary.",
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
    case ScreenMovePick:
        draw_move_pick(app, canvas);
        break;
    case ScreenBattleSwap:
        draw_battle_swap(app, canvas);
        break;
    case ScreenBattleResult:
        draw_battle_result(app, canvas);
        break;
    case ScreenJackIn:
        draw_jack_in(app, canvas);
        break;
    case ScreenDungeon:
        draw_dungeon(app, canvas);
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
    case ScreenSwap:
        draw_swap(app, canvas);
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
    SaveData* sv = &app->s.save;
    switch(row) {
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
    SaveData* sv = &app->s.save;
    switch(row) {
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
        session_set_bluetooth(&app->s, !sv->bt_enabled);
        app->s.msg[0] = '\0';
        return;
    case BoardLog:
        sv->log_sd = !sv->log_sd;
        if(sv->log_sd) app_toast(app, "Saves to apps_data/echoids");
        break;
    default:
        return;
    }
    app->s.dirty = true;
}

/* The rows shown, in order. */
static const uint8_t board_rows[] = {
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
};
#define BOARD_ROW_COUNT ((uint8_t)(sizeof(board_rows) / sizeof(board_rows[0])))

static void draw_board(App* app, Canvas* canvas) {
    draw_title(canvas, BOARD_TITLE, "<> change");
    char buf[32];
    for(uint8_t r = 0; r < BOARD_ROWS; r++) {
        uint8_t i = app->board_scroll + r;
        if(i >= BOARD_ROW_COUNT) break;
        int y = 22 + r * 10;
        draw_row(canvas, y, i == app->board_idx);
        board_row_text(app, board_rows[i], buf, sizeof(buf));
        canvas_draw_str(canvas, 3, y, buf);
    }
    canvas_set_color(canvas, ColorBlack);
    if(BOARD_ROW_COUNT > BOARD_ROWS)
        elements_scrollbar_pos(canvas, 127, 14, 48, app->board_idx, BOARD_ROW_COUNT);
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
        uart_short(app->s.save.uart),
        (unsigned long)scanner_baud_rates[app->s.save.baud_idx % scanner_baud_count]);
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
            app->s.save.uart = cycle(app->s.save.uart, ScanUartCount, 1);
        else
            app->s.save.baud_idx = cycle(app->s.save.baud_idx, scanner_baud_count, 1);
        app->s.dirty = true;
        terminal_open(app);
    } else if(press && ev->key == InputKeyBack) {
        terminal_close(app);
        app->screen = ScreenBoard;
        if(app->s.dirty) session_save(&app->s);
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
    if(app->s.save.power % ScanPowerCount != ScanPowerUntouched) {
        app->cal_otg_by_us = scanner_power_on();
        if(app->cal_otg_by_us) wait = scanner_boot_waits_ms[app->s.save.boot_idx % scanner_boot_count];
    }
    app->cal_tick = furi_get_tick() + furi_ms_to_ticks(wait);
}

static void calibrate_stop(App* app) {
    scanner_stop(app->scanner);
    if(app->cal_otg_by_us && app->s.save.power % ScanPowerCount == ScanPowerAuto) {
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
        app->s.save.uart = uart;
        app->s.save.baud_idx = baud;
        app->s.dirty = true;
        session_save(&app->s);
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
    if(app->cal_otg_by_us && app->s.save.power % ScanPowerCount == ScanPowerAuto) {
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
    if(app->board_idx >= BOARD_ROW_COUNT) app->board_idx = 0;
    uint8_t row = board_rows[app->board_idx];
    if(dy) {
        list_move(&app->board_idx, &app->board_scroll, BOARD_ROW_COUNT, BOARD_ROWS, dy);
    } else if(press && (ev->key == InputKeyLeft || ev->key == InputKeyRight)) {
        board_change(app, row, ev->key == InputKeyLeft ? -1 : 1);
    } else if(press && ev->key == InputKeyOk) {
        if(row == BoardTerminal) {
            app->term_otg_by_us = false;
            terminal_open(app);
            app->screen = ScreenTerminal;
        } else if(row == BoardCalibrate) {
            calibrate_start(app);
            app->screen = ScreenCalibrate;
        } else {
            board_change(app, row, 1);
        }
    } else if(press && ev->key == InputKeyBack) {
        app->screen = ScreenSettings;
        if(app->s.dirty) session_save(&app->s);
    }
}

/* Shows the session's message for the last action, if it has one. */
static void app_say(App* app) {
    if(app->s.msg[0]) app_toast(app, app->s.msg);
    app->s.msg[0] = '\0';
}

static void collect_start(App* app) {
    session_scan_start(&app->s);
    app->scan_started = furi_get_tick();
    ScanConfig cfg;
    app_scan_config(app, &cfg);
    if(!scanner_start(app->scanner, &cfg)) {
        app_toast(app, scanner_error(app->scanner) ? scanner_error(app->scanner) : "Scanner error");
        notification_message(app->notif, &sequence_error);
    }
    app->screen = ScreenCollect;
}

static void collect_stop(App* app) {
    scanner_stop(app->scanner);
    session_scan_stop(&app->s);
    app->screen = ScreenMenu;
}

static void battle_begin(App* app) {
    if(app->s.nearby_count == 0) return;
    if(!session_battle_begin(&app->s, app->nearby_idx)) {
        app_say(app);
        return;
    }
    app->battle_menu = 0;
    app->screen = ScreenBattle;
    notification_message(app->notif, &sequence_single_vibro);
}

/* Opens the switch picker; forced when the active echoid has fainted. */
static void battle_swap_open(App* app) {
    if(!session_battle_others_can_fight(&app->s)) {
        app_toast(app, "No one else can fight!");
        return;
    }
    /* Start on the first team member that can go in. */
    app->bswap_idx = 0;
    app->bswap_scroll = 0;
    for(uint8_t i = 0; i < app->s.save.team_count; i++) {
        if(session_battle_can_switch_to(&app->s, i)) {
            app->bswap_idx = i;
            break;
        }
    }
    app->s.msg[0] = '\0';
    list_move(&app->bswap_idx, &app->bswap_scroll, app->s.save.team_count, TEAM_ROWS, 0);
    app->screen = ScreenBattleSwap;
}

/* After a battle action: the right screen and a sound for what happened. */
static void battle_after_action(App* app) {
    Battle* b = &app->s.battle;
    app->screen = ScreenBattle;
    if(app->s.must_switch) {
        battle_swap_open(app);
        return;
    }
    if(b->result == BattleOngoing) return;
    pac_invalidate(app); /* a catch may have gone to the PAC */
    switch(b->result) {
    case BattleCaptured:
        notification_message(app->notif, &sequence_success);
        break;
    case BattleWon:
        notification_message(app->notif, &sequence_semi_success);
        break;
    case BattleLost:
        notification_message(app->notif, &sequence_error);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------------- */
/* Jacking in and dungeons                                                   */
/* ------------------------------------------------------------------------- */

typedef struct {
    ViewDispatcher* dispatcher;
    bool entered;
} PasswordPrompt;

static void password_done(void* ctx) {
    PasswordPrompt* p = ctx;
    p->entered = true;
    view_dispatcher_stop(p->dispatcher);
}

static bool password_back(void* ctx) {
    PasswordPrompt* p = ctx;
    view_dispatcher_stop(p->dispatcher);
    return true;
}

/* Asks for a network's password on the Flipper keyboard (empty for an open
 * network). Our own screen is set aside meanwhile. False if cancelled. */
static bool ask_password(App* app, const char* ssid, char* out, size_t size) {
    view_port_enabled_set(app->view_port, false);
    gui_remove_view_port(app->gui, app->view_port);

    PasswordPrompt prompt = {.dispatcher = view_dispatcher_alloc(), .entered = false};
    TextInput* input = text_input_alloc();
    char header[40];
    snprintf(header, sizeof(header), "%.18s password (none: OK)", ssid);
    text_input_set_header_text(input, header);
    text_input_set_minimum_length(input, 0);
    out[0] = '\0';
    text_input_set_result_callback(input, password_done, &prompt, out, size, true);
    view_dispatcher_add_view(prompt.dispatcher, 0, text_input_get_view(input));
    view_dispatcher_set_event_callback_context(prompt.dispatcher, &prompt);
    view_dispatcher_set_navigation_event_callback(prompt.dispatcher, password_back);
    view_dispatcher_attach_to_gui(prompt.dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(prompt.dispatcher, 0);
    view_dispatcher_run(prompt.dispatcher);

    view_dispatcher_remove_view(prompt.dispatcher, 0);
    view_dispatcher_free(prompt.dispatcher);
    text_input_free(input);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    view_port_enabled_set(app->view_port, true);
    return prompt.entered;
}

/* Jack in to the selected echoid's network: password, then the board joins it. */
static void jack_in_start(App* app) {
    uint8_t idx = app->nearby_idx;
    if(!session_jack_in_check(&app->s, idx)) {
        app_say(app);
        return;
    }
    const Nearby* n = &app->s.nearby[idx];
    if(!n->ssid[0] || strcmp(n->ssid, "<hidden>") == 0) {
        app_toast(app, "Hidden networks can't join");
        return;
    }
    /* Stop scanning: the board can't scan and connect at once. */
    scanner_stop(app->scanner);
    session_scan_stop(&app->s);
    char password[64];
    if(!ask_password(app, n->ssid, password, sizeof(password))) {
        collect_start(app);
        return;
    }
    ScanConfig cfg;
    app_scan_config(app, &cfg);
    app->jack_index = idx;
    bool started = scanner_join(app->scanner, &cfg, n->ssid, password);
    memset(password, 0, sizeof(password));
    if(!started) {
        app_toast(app, scanner_error(app->scanner) ? scanner_error(app->scanner) : "Scanner error");
        collect_start(app);
        return;
    }
    app->screen = ScreenJackIn;
}

/* Watches the board connect, then enters the dungeon. */
static void jack_in_tick(App* app) {
    ScanJoinState st = scanner_join_state(app->scanner);
    if(st == ScanJoinConnected) {
        session_link_up(&app->s, app->s.nearby[app->jack_index].mac);
        if(session_jack_in(&app->s, app->jack_index)) {
            notification_message(app->notif, &sequence_success);
            app_say(app);
            app->screen = ScreenDungeon;
            return;
        }
        app_say(app);
    } else if(st == ScanJoinFailed) {
        app_toast(app, scanner_error(app->scanner) ? scanner_error(app->scanner) : "Couldn't connect");
        notification_message(app->notif, &sequence_error);
    } else {
        return;
    }
    scanner_stop(app->scanner);
    session_link_down(&app->s);
    collect_start(app);
}

static void jack_out(App* app) {
    if(session_dungeon_leave(&app->s)) {
        scanner_stop(app->scanner); /* the board leaves the network */
        session_link_down(&app->s);
        app->menu_idx = 0;
        app->menu_scroll = 0;
    }
    app_say(app);
}

/* OK in the dungeon: play this room, or go deeper once it's done. */
static void dungeon_ok(App* app) {
    const Dungeon* d = &app->s.dungeon;
    if(d->cleared) {
        app_toast(app, "Cleared! Jack out: menu");
    } else if(d->room_done) {
        session_dungeon_next(&app->s);
        app_say(app);
    } else if(session_dungeon_room(&app->s)) {
        if(app->s.in_battle) {
            app->dungeon_battle = true;
            app->battle_menu = 0;
            app->screen = ScreenBattle;
            notification_message(app->notif, &sequence_single_vibro);
        } else {
            notification_message(app->notif, &sequence_success);
        }
        app_say(app);
    } else {
        app_say(app);
    }
}

/* Where to go when a battle is over. */
static Screen after_battle_screen(App* app) {
    bool was_dungeon = app->dungeon_battle;
    app->dungeon_battle = false;
    if(session_in_dungeon(&app->s)) return ScreenDungeon;
    if(was_dungeon) {
        /* Thrown out of the dungeon */
        scanner_stop(app->scanner);
        session_link_down(&app->s);
        app->menu_idx = 0;
        return ScreenMenu;
    }
    return ScreenCollect;
}

/* ------------------------------------------------------------------------- */
/* Input                                                                     */
/* ------------------------------------------------------------------------- */

/* 32-bit list navigation for the PAC, which can hold any number of echoids. */
static void list_move32(uint32_t* idx, uint32_t* scroll, uint32_t count, uint32_t rows, int delta) {
    if(count == 0) {
        *idx = 0;
        *scroll = 0;
        return;
    }
    if(delta < 0)
        *idx = *idx == 0 ? count - 1 : *idx - 1;
    else if(delta > 0)
        *idx = *idx + 1 >= count ? 0 : *idx + 1;
    if(*idx >= count) *idx = count - 1;
    if(*idx < *scroll) *scroll = *idx;
    if(*idx >= *scroll + rows) *scroll = *idx - rows + 1;
    if(*scroll >= count) *scroll = 0;
}

static void pac_view_load(App* app) {
    if(app->s.save.pac_count == 0 || !pac_read(app->pac_idx, &app->pac_view))
        memset(&app->pac_view, 0, sizeof(app->pac_view));
}

/* Opens the Team screen on a tab with the given position selected. */
static void team_open(App* app, uint8_t tab, uint32_t select) {
    app->team_tab = tab;
    if(tab == 0) {
        app->team_idx = (uint8_t)select;
        app->team_scroll = 0;
        list_move(&app->team_idx, &app->team_scroll, app->s.save.team_count, TEAM_ROWS, 0);
    } else {
        app->pac_idx = select;
        app->pac_scroll = 0;
        list_move32(&app->pac_idx, &app->pac_scroll, app->s.save.pac_count, TEAM_ROWS, 0);
        pac_invalidate(app);
        pac_refresh(app);
    }
    app->detail_menu = false;
    app->screen = ScreenTeam;
}

/* Team member -> end of the PAC. */
static void team_send_to_pac(App* app, uint8_t idx) {
    bool ok = session_team_to_pac(&app->s, idx);
    if(ok) {
        pac_invalidate(app);
        team_open(app, 0, app->s.save.lead);
    }
    app_say(app);
}

/* PAC echoid at pac_idx -> team. */
static void pac_send_to_team(App* app) {
    bool ok = session_pac_to_team(&app->s, app->pac_idx);
    if(ok) {
        pac_invalidate(app);
        team_open(app, 0, app->s.save.team_count - 1);
    }
    app_say(app);
}

static void detail_input(App* app, InputEvent* ev, bool press, int dy) {
    uint32_t pos, n;
    detail_target(app, &pos, &n);

    if(app->detail_menu) {
        uint8_t count = app->team_tab == 0 ? 2 : 1;
        if(dy) {
            app->detail_menu_idx = (uint8_t)((app->detail_menu_idx + count + dy) % count);
        } else if(press && ev->key == InputKeyBack) {
            app->detail_menu = false;
        } else if(press && ev->key == InputKeyOk) {
            app->detail_menu = false;
            if(app->team_tab == 0 && app->detail_menu_idx == 0) {
                if(session_set_lead(&app->s, (uint8_t)pos))
                    notification_message(app->notif, &sequence_blink_green_10);
                app_say(app);
            } else if(app->team_tab == 0) {
                team_send_to_pac(app, (uint8_t)pos);
            } else if(!session_team_full(&app->s)) {
                pac_send_to_team(app);
            } else {
                /* Team full: choose who goes into the PAC in its place. */
                app->swap_idx = 0;
                app->swap_scroll = 0;
                app->screen = ScreenSwap;
                app_toast(app, "Team full: pick a swap");
            }
        }
        return;
    }

    if(press && (ev->key == InputKeyLeft || ev->key == InputKeyRight)) {
        int d = ev->key == InputKeyLeft ? -1 : 1;
        if(app->team_tab == 0) {
            list_move(&app->team_idx, &app->team_scroll, app->s.save.team_count, TEAM_ROWS, d);
        } else {
            list_move32(&app->pac_idx, &app->pac_scroll, app->s.save.pac_count, TEAM_ROWS, d);
            pac_view_load(app);
            pac_refresh(app);
        }
    } else if(dy) {
        /* Stats -> Moves -> Rating pages */
        app->detail_page = (uint8_t)((app->detail_page + DetailPageCount + dy) % DetailPageCount);
    } else if(press && ev->key == InputKeyOk) {
        app->detail_menu = true;
        app->detail_menu_idx = 0;
    } else if(press && ev->key == InputKeyBack) {
        app->screen = ScreenTeam;
    }
}

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

    case ScreenMenu: {
        uint8_t count;
        const uint8_t* items = menu_list(app, &count);
        if(app->menu_idx >= count) app->menu_idx = 0;
        if(dy) {
            list_move(&app->menu_idx, &app->menu_scroll, count, MENU_ROWS, dy);
            app->confirm_jack_out = false;
        } else if(press && ev->key == InputKeyOk) {
            switch(items[app->menu_idx]) {
            case MenuDungeon:
                app->screen = ScreenDungeon;
                break;
            case MenuJackOut:
                if(!app->confirm_jack_out) {
                    app->confirm_jack_out = true;
                } else {
                    app->confirm_jack_out = false;
                    jack_out(app);
                }
                break;
            case MenuCollect:
                collect_start(app);
                break;
            case MenuTeam:
                team_open(app, 0, app->s.save.lead);
                break;
            case MenuPac:
                team_open(app, 1, 0); /* straight to Cold PAC Storage */
                break;
            case MenuCraft:
                app->screen = ScreenCraft;
                break;
            case MenuBag:
                app->screen = ScreenBag;
                break;
            case MenuSettings:
                app->confirm_reset = false;
                app->screen = ScreenSettings;
                break;
            }
        } else if(press && ev->key == InputKeyBack) {
            if(app->confirm_jack_out)
                app->confirm_jack_out = false;
            else
                app->running = false;
        }
        break;
    }

    case ScreenCollect:
        if(app->collect_menu) {
            /* Battle the echoid, or jack in to its network */
            if(dy) {
                app->collect_menu_idx ^= 1;
            } else if(press && ev->key == InputKeyBack) {
                app->collect_menu = false;
            } else if(press && ev->key == InputKeyOk) {
                app->collect_menu = false;
                if(app->collect_menu_idx == 0)
                    battle_begin(app);
                else
                    jack_in_start(app);
            }
        } else if(dy) {
            list_move(&app->nearby_idx, &app->nearby_scroll, app->s.nearby_count, NEARBY_ROWS, dy);
        } else if(press && ev->key == InputKeyOk && app->s.nearby_count > 0) {
            app->collect_menu = true;
            app->collect_menu_idx = 0;
        } else if(press && ev->key == InputKeyBack) {
            collect_stop(app);
        }
        break;

    case ScreenJackIn:
        if(press && ev->key == InputKeyBack) {
            scanner_stop(app->scanner);
            app_toast(app, "Jack in cancelled");
            collect_start(app);
        }
        break;

    case ScreenDungeon:
        if(press && ev->key == InputKeyOk) {
            dungeon_ok(app);
        } else if(press && ev->key == InputKeyBack) {
            app->menu_idx = 0;
            app->menu_scroll = 0;
            app->screen = ScreenMenu;
        }
        break;

    case ScreenBattle:
        if(app->s.battle.result != BattleOngoing) {
            if(press && (ev->key == InputKeyOk || ev->key == InputKeyBack)) {
                session_battle_close(&app->s);
                app_say(app); /* e.g. ejected from the dungeon */
                app->result_scroll = 0;
                app->screen = app->s.battle.xp_gained ? ScreenBattleResult : after_battle_screen(app);
            }
            break;
        }
        if(press && ev->key == InputKeyLeft) app->battle_menu = (app->battle_menu + 3) % 4;
        if(press && ev->key == InputKeyRight) app->battle_menu = (app->battle_menu + 1) % 4;
        if(press && ev->key == InputKeyOk) {
            switch(app->battle_menu) {
            case 0: {
                Move moves[MAX_MOVES];
                if(app->move_idx >= echo_moves(app->s.battle.mine, moves)) app->move_idx = 0;
                app->screen = ScreenMovePick;
                break;
            }
            case 1:
                battle_swap_open(app);
                break;
            case 2: {
                const char* why = session_catch_blocked(&app->s);
                if(why)
                    app_toast(app, why);
                else
                    app->screen = ScreenBallPick;
                break;
            }
            case 3:
                session_battle_run(&app->s);
                battle_after_action(app);
                break;
            }
        }
        break;

    case ScreenBattleResult:
        if(dy) {
            int n = (int)app->result_scroll + dy;
            int max = app->s.result_count > RESULT_ROWS ? app->s.result_count - RESULT_ROWS : 0;
            if(n < 0) n = 0;
            if(n > max) n = max;
            app->result_scroll = (uint8_t)n;
        } else if(press && (ev->key == InputKeyOk || ev->key == InputKeyBack)) {
            app->screen = after_battle_screen(app);
        }
        break;

    case ScreenBattleSwap: {
        if(dy) {
            list_move(&app->bswap_idx, &app->bswap_scroll, app->s.save.team_count, TEAM_ROWS, dy);
        } else if(press && ev->key == InputKeyBack && !app->s.must_switch) {
            app->screen = ScreenBattle;
        } else if(press && ev->key == InputKeyOk) {
            if(session_battle_switch(&app->s, app->bswap_idx)) {
                app->move_idx = 0;
                battle_after_action(app);
            } else {
                app_say(app);
            }
        }
        break;
    }

    case ScreenMovePick: {
        Move moves[MAX_MOVES];
        uint8_t n = echo_moves(app->s.battle.mine, moves);
        if(dy && n) app->move_idx = (uint8_t)((app->move_idx + n + dy) % n);
        if(press && ev->key == InputKeyBack) app->screen = ScreenBattle;
        if(press && ev->key == InputKeyOk) {
            /* The move stays selected for next turn. */
            session_battle_move(&app->s, app->move_idx);
            battle_after_action(app);
        }
        break;
    }

    case ScreenBallPick:
        if(dy) app->ball_idx = (uint8_t)((app->ball_idx + 3 + dy) % 3);
        if(press && ev->key == InputKeyBack) app->screen = ScreenBattle;
        if(press && ev->key == InputKeyOk) {
            session_battle_ball(&app->s, app->ball_idx);
            app->s.msg[0] = '\0'; /* the battle log says what happened */
            battle_after_action(app);
        }
        break;

    case ScreenTeam: {
        bool pac = app->team_tab == 1;
        uint32_t n = pac ? app->s.save.pac_count : app->s.save.team_count;
        if(dy) {
            if(pac) {
                list_move32(&app->pac_idx, &app->pac_scroll, n, TEAM_ROWS, dy);
                pac_refresh(app);
            } else {
                list_move(&app->team_idx, &app->team_scroll, (uint8_t)n, TEAM_ROWS, dy);
            }
        } else if(press && (ev->key == InputKeyLeft || ev->key == InputKeyRight)) {
            app->team_tab ^= 1; /* Team <-> PAC */
            if(app->team_tab == 1) {
                list_move32(&app->pac_idx, &app->pac_scroll, app->s.save.pac_count, TEAM_ROWS, 0);
                pac_invalidate(app);
                pac_refresh(app);
            }
        } else if(press && ev->key == InputKeyOk && n > 0) {
            if(pac) pac_view_load(app);
            app->detail_menu = false;
            app->screen = ScreenDetail;
        } else if(press && ev->key == InputKeyBack) {
            app->screen = ScreenMenu;
        }
        break;
    }

    case ScreenDetail:
        detail_input(app, ev, press, dy);
        break;

    case ScreenSwap: {
        if(dy) {
            list_move(&app->swap_idx, &app->swap_scroll, app->s.save.team_count, TEAM_ROWS, dy);
        } else if(press && ev->key == InputKeyOk && app->swap_idx < app->s.save.team_count) {
            /* The team member takes the PAC echoid's slot, and vice versa. */
            if(session_swap(&app->s, app->pac_idx, app->swap_idx)) {
                pac_invalidate(app);
                notification_message(app->notif, &sequence_success);
                team_open(app, 0, app->swap_idx);
            }
            app_say(app);
        } else if(press && ev->key == InputKeyBack) {
            app->screen = ScreenDetail;
        }
        break;
    }

    case ScreenCraft:
        if(dy) {
            list_move(&app->craft_idx, &app->craft_scroll, recipe_count, CRAFT_ROWS, dy);
        } else if(press && ev->key == InputKeyOk) {
            bool ok = session_craft(&app->s, app->craft_idx);
            notification_message(app->notif, ok ? &sequence_success : &sequence_error);
            app_say(app);
        } else if(press && ev->key == InputKeyBack) {
            app->screen = ScreenMenu;
        }
        break;

    case ScreenBag:
        if(press && (ev->key == InputKeyLeft || ev->key == InputKeyRight)) {
            app->bag_tab ^= 1;
        } else if(dy && app->bag_tab == 0) {
            list_move(&app->bag_idx, &app->bag_scroll, ItemCount, BAG_ROWS, dy);
        } else if(press && ev->key == InputKeyOk && app->bag_tab == 0) {
            uint8_t item = app->bag_idx;
            if(session_item_use(item) == ItemUseNone) {
                app_toast(app, "Use balls in battle");
            } else if(app->s.save.inv.items[item] == 0) {
                app_toast(app, "You have none");
            } else {
                /* Choose who to use it on, starting at the lead. */
                app->use_item_id = item;
                app->use_idx = app->s.save.lead;
                app->use_scroll = 0;
                list_move(&app->use_idx, &app->use_scroll, app->s.save.team_count, TEAM_ROWS, 0);
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
                    if(session_reset(&app->s)) {
                        pac_invalidate(app);
                        app->nearby_idx = 0;
                        app->nearby_scroll = 0;
                        app->screen = ScreenIntro;
                    }
                    app_say(app);
                }
                break;
            }
        } else if(press && ev->key == InputKeyBack) {
            app->confirm_reset = false;
            app->screen = ScreenMenu;
            if(app->s.dirty) session_save(&app->s);
        }
        break;

    case ScreenBoard:
        board_input(app, ev, press, dy);
        break;

    case ScreenTerminal:
        terminal_input(app, ev, press);
        break;

    case ScreenUseOn: {
        if(dy) {
            list_move(&app->use_idx, &app->use_scroll, app->s.save.team_count, TEAM_ROWS, dy);
        } else if(press && ev->key == InputKeyOk && app->use_idx < app->s.save.team_count) {
            /* On success go back to the Bag (the toast shows there); on a
             * refusal (already healthy, max level) stay so another can be picked. */
            bool ok = session_use_item(&app->s, app->use_item_id, app->use_idx);
            if(ok) {
                notification_message(app->notif, &sequence_success);
                app->screen = ScreenBag;
            }
            app_say(app);
        } else if(press && ev->key == InputKeyBack) {
            app->screen = ScreenBag;
        }
        break;
    }

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
    /* Sightings from the board go to the session (not while debugging it). */
    ScanEvent ev;
    bool debug = app->screen == ScreenTerminal || app->screen == ScreenCalibrate;
    for(int i = 0; i < 8 && scanner_poll(app->scanner, &ev); i++)
        if(!debug) session_sighting(&app->s, &ev);

    if(app->screen == ScreenCalibrate) calibrate_tick(app);
    if(app->screen == ScreenJackIn) jack_in_tick(app);

    session_tick(&app->s);

    /* The nearby list can shrink when the hour changes. */
    if(app->nearby_idx >= app->s.nearby_count)
        app->nearby_idx = app->s.nearby_count ? app->s.nearby_count - 1 : 0;
    if(app->nearby_scroll > app->nearby_idx) app->nearby_scroll = app->nearby_idx;

    /* What happened while scanning: a popup and a blink. */
    SessionEvent se;
    while(session_take_event(&app->s, &se)) {
        switch(se.kind) {
        case SessionEventSpotted:
            notification_message(app->notif, &sequence_blink_green_10);
            break;
        case SessionEventShard:
            app_toast(app, se.text);
            app->toast_important = true;
            notification_message(
                app->notif,
                se.shard.tier == ShardPrismatic ? &sequence_blink_magenta_100 : &sequence_blink_cyan_10);
            break;
        case SessionEventRecharge:
            /* Don't cover a new shard's popup with a recharge note. */
            if(!(app->toast_important && app_toast_showing(app))) app_toast(app, se.text);
            break;
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Entry point                                                               */
/* ------------------------------------------------------------------------- */

int32_t echoids_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    /* The starter is born from this Flipper's own Bluetooth MAC. */
    const char* name = furi_hal_version_get_name_ptr();
    bool fresh = session_init(&app->s, furi_hal_version_get_ble_mac(), name ? name : "Flipper");
    app->screen = fresh ? ScreenIntro : ScreenMenu;

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
    session_save(&app->s);

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
