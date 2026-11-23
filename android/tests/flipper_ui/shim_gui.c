/* Canvas, view port and GUI elements for the shim: draws into a 128x64
 * framebuffer that the platform layer shows on screen. */
#include <pthread.h>

#include "font5x7.h"
#include "furi.h"
#include "gui/elements.h"
#include "gui/gui.h"
#include "shim.h"

struct Canvas {
    uint8_t (*fb)[CANVAS_WIDTH];
    Color color;
    Font font;
};

struct ViewPort {
    ViewPortDrawCallback draw;
    void* draw_ctx;
    ViewPortInputCallback input;
    void* input_ctx;
    bool enabled;
};

static pthread_mutex_t gui_lock = PTHREAD_MUTEX_INITIALIZER;
static ViewPort* active;
static volatile bool dirty = true;
static uint32_t input_sequence;

/* ------------------------------------------------------------------------- */
/* Pixels and shapes                                                         */
/* ------------------------------------------------------------------------- */

static void px(Canvas* c, int32_t x, int32_t y) {
    if(x < 0 || y < 0 || x >= CANVAS_WIDTH || y >= CANVAS_HEIGHT) return;
    switch(c->color) {
    case ColorBlack:
        c->fb[y][x] = 1;
        break;
    case ColorWhite:
        c->fb[y][x] = 0;
        break;
    default:
        c->fb[y][x] ^= 1;
        break;
    }
}

void canvas_clear(Canvas* c) {
    memset(c->fb, 0, CANVAS_WIDTH * CANVAS_HEIGHT);
}

void canvas_set_color(Canvas* c, Color color) {
    c->color = color;
}

void canvas_set_font(Canvas* c, Font font) {
    c->font = font;
}

void canvas_draw_dot(Canvas* c, int32_t x, int32_t y) {
    px(c, x, y);
}

void canvas_draw_box(Canvas* c, int32_t x, int32_t y, size_t w, size_t h) {
    for(size_t j = 0; j < h; j++)
        for(size_t i = 0; i < w; i++)
            px(c, x + (int32_t)i, y + (int32_t)j);
}

static void hline(Canvas* c, int32_t x, int32_t y, int32_t w) {
    for(int32_t i = 0; i < w; i++)
        px(c, x + i, y);
}

static void vline(Canvas* c, int32_t x, int32_t y, int32_t h) {
    for(int32_t j = 0; j < h; j++)
        px(c, x, y + j);
}

void canvas_draw_frame(Canvas* c, int32_t x, int32_t y, size_t w, size_t h) {
    if(!w || !h) return;
    hline(c, x, y, (int32_t)w);
    hline(c, x, y + (int32_t)h - 1, (int32_t)w);
    vline(c, x, y + 1, (int32_t)h - 2);
    vline(c, x + (int32_t)w - 1, y + 1, (int32_t)h - 2);
}

/* Quarter circle: quadrant bits 1 = top-right, 2 = top-left, 4 = bottom-left, 8 = bottom-right. */
static void arc(Canvas* c, int32_t cx, int32_t cy, int32_t r, int quadrants) {
    int32_t x = r, y = 0, err = 1 - r;
    while(x >= y) {
        int32_t pts[8][2] = {{x, -y}, {y, -x}, {-y, -x}, {-x, -y}, {-x, y}, {-y, x}, {y, x}, {x, y}};
        for(int k = 0; k < 8; k++) {
            int q = k / 2; /* 0 TR, 1 TL, 2 BL, 3 BR */
            if(quadrants & (1 << q)) px(c, cx + pts[k][0], cy + pts[k][1]);
        }
        y++;
        if(err < 0)
            err += 2 * y + 1;
        else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void canvas_draw_rframe(Canvas* c, int32_t x, int32_t y, size_t w, size_t h, size_t radius) {
    int32_t r = (int32_t)radius;
    int32_t W = (int32_t)w, H = (int32_t)h;
    if(r * 2 > W) r = W / 2;
    if(r * 2 > H) r = H / 2;
    hline(c, x + r, y, W - 2 * r);
    hline(c, x + r, y + H - 1, W - 2 * r);
    vline(c, x, y + r, H - 2 * r);
    vline(c, x + W - 1, y + r, H - 2 * r);
    arc(c, x + W - 1 - r, y + r, r, 1);
    arc(c, x + r, y + r, r, 2);
    arc(c, x + r, y + H - 1 - r, r, 4);
    arc(c, x + W - 1 - r, y + H - 1 - r, r, 8);
}

void canvas_draw_line(Canvas* c, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    int32_t dx = x2 > x1 ? x2 - x1 : x1 - x2, sx = x1 < x2 ? 1 : -1;
    int32_t dy = y2 > y1 ? y1 - y2 : y2 - y1, sy = y1 < y2 ? 1 : -1;
    int32_t err = dx + dy;
    for(;;) {
        px(c, x1, y1);
        if(x1 == x2 && y1 == y2) break;
        int32_t e2 = 2 * err;
        if(e2 >= dy) {
            err += dy;
            x1 += sx;
        }
        if(e2 <= dx) {
            err += dx;
            y1 += sy;
        }
    }
}

void canvas_draw_circle(Canvas* c, int32_t x, int32_t y, size_t radius) {
    arc(c, x, y, (int32_t)radius, 0xF);
}

/* ------------------------------------------------------------------------- */
/* Text                                                                      */
/* ------------------------------------------------------------------------- */

static int glyph_index(char ch) {
    unsigned char u = (unsigned char)ch;
    if(u < 32 || u > 126) return '?' - 32;
    return u - 32;
}

/* Horizontal advance of one character in the current font. */
static int advance(Canvas* c, char ch) {
    int w = font5x7_width[glyph_index(ch)];
    switch(c->font) {
    case FontPrimary:
        return w + 2; /* bold: drawn twice, one pixel apart */
    case FontKeyboard:
        return 6; /* monospaced */
    default:
        return w + 1;
    }
}

uint16_t canvas_string_width(Canvas* c, const char* str) {
    int w = 0;
    for(const char* p = str; *p; p++)
        w += advance(c, *p);
    if(w > 0) w -= 1; /* no spacing after the last character */
    return (uint16_t)w;
}

static void draw_glyph(Canvas* c, int32_t x, int32_t baseline, char ch) {
    int i = glyph_index(ch);
    int w = font5x7_width[i];
    int32_t x0 = c->font == FontKeyboard ? x + (5 - w) / 2 : x;
    for(int r = 0; r < 8; r++) {
        uint8_t bits = font5x7_rows[i][r];
        for(int col = 0; col < w; col++) {
            if(bits & (0x10 >> col)) {
                px(c, x0 + col, baseline - 6 + r);
                if(c->font == FontPrimary) px(c, x0 + col + 1, baseline - 6 + r);
            }
        }
    }
}

void canvas_draw_str(Canvas* c, int32_t x, int32_t y, const char* str) {
    for(const char* p = str; *p; p++) {
        draw_glyph(c, x, y, *p);
        x += advance(c, *p);
    }
}

void canvas_draw_str_aligned(
    Canvas* c,
    int32_t x,
    int32_t y,
    Align horizontal,
    Align vertical,
    const char* str) {
    int32_t w = canvas_string_width(c, str);
    if(horizontal == AlignRight)
        x -= w;
    else if(horizontal == AlignCenter)
        x -= w / 2;
    /* y is the baseline for AlignBottom; capitals are 7 px tall. */
    if(vertical == AlignTop)
        y += 6;
    else if(vertical == AlignCenter)
        y += 3;
    canvas_draw_str(c, x, y, str);
}

/* ------------------------------------------------------------------------- */
/* Elements                                                                  */
/* ------------------------------------------------------------------------- */

void elements_progress_bar(Canvas* c, int32_t x, int32_t y, size_t width, float progress) {
    if(progress < 0) progress = 0;
    if(progress > 1) progress = 1;
    canvas_draw_rframe(c, x, y, width, 9, 3);
    size_t fill = (size_t)((float)(width - 4) * progress);
    if(fill) canvas_draw_box(c, x + 2, y + 2, fill, 5);
}

void elements_scrollbar_pos(Canvas* c, int32_t x, int32_t y, size_t height, size_t pos, size_t total) {
    if(total == 0 || height == 0) return;
    /* Dotted track with a solid thumb. */
    for(size_t j = 0; j < height; j += 2)
        px(c, x, y + (int32_t)j);
    size_t block = height / total;
    if(block < 3) block = 3;
    if(block > height) block = height;
    size_t top = total > 1 ? (height - block) * (pos < total ? pos : total - 1) / (total - 1) : 0;
    canvas_draw_box(c, x - 1, y + (int32_t)top, 3, block);
}

void elements_scrollbar(Canvas* c, size_t pos, size_t total) {
    elements_scrollbar_pos(c, CANVAS_WIDTH - 1, 0, CANVAS_HEIGHT, pos, total);
}

void elements_button_center(Canvas* c, const char* str) {
    /* A tab at the bottom centre: an OK "button" icon and the label. */
    Font old = c->font;
    c->font = FontSecondary;
    int32_t tw = canvas_string_width(c, str);
    int32_t w = tw + 15, h = 11;
    int32_t x = (CANVAS_WIDTH - w) / 2, y = CANVAS_HEIGHT - h;
    Color color = c->color;
    c->color = ColorBlack;
    canvas_draw_box(c, x + 1, y, (size_t)w - 2, (size_t)h);
    canvas_draw_box(c, x, y + 1, (size_t)w, (size_t)h - 1);
    c->color = ColorWhite;
    canvas_draw_circle(c, x + 6, y + 5, 3);
    canvas_draw_dot(c, x + 6, y + 5);
    canvas_draw_str(c, x + 12, y + 9, str);
    c->color = color;
    c->font = old;
}

/* ------------------------------------------------------------------------- */
/* View port                                                                 */
/* ------------------------------------------------------------------------- */

ViewPort* view_port_alloc(void) {
    ViewPort* vp = calloc(1, sizeof(ViewPort));
    vp->enabled = true;
    return vp;
}

void view_port_free(ViewPort* vp) {
    free(vp);
}

void view_port_draw_callback_set(ViewPort* vp, ViewPortDrawCallback cb, void* ctx) {
    vp->draw = cb;
    vp->draw_ctx = ctx;
}

void view_port_input_callback_set(ViewPort* vp, ViewPortInputCallback cb, void* ctx) {
    vp->input = cb;
    vp->input_ctx = ctx;
}

void view_port_update(ViewPort* vp) {
    UNUSED(vp);
    dirty = true;
}

void view_port_enabled_set(ViewPort* vp, bool enabled) {
    vp->enabled = enabled;
}

void gui_add_view_port(Gui* gui, ViewPort* vp, GuiLayer layer) {
    UNUSED(gui);
    UNUSED(layer);
    pthread_mutex_lock(&gui_lock);
    active = vp;
    dirty = true;
    pthread_mutex_unlock(&gui_lock);
}

void gui_remove_view_port(Gui* gui, ViewPort* vp) {
    UNUSED(gui);
    pthread_mutex_lock(&gui_lock);
    if(active == vp) active = NULL;
    pthread_mutex_unlock(&gui_lock);
}

bool shim_take_dirty(void) {
    bool d = dirty;
    dirty = false;
    return d;
}

void shim_render(uint8_t fb[64][128]) {
    /* Draw into a persistent buffer: if the game skips a frame (its draw
     * callback returns early when busy), the previous frame stays. */
    static uint8_t screen[CANVAS_HEIGHT][CANVAS_WIDTH];
    Canvas canvas = {.fb = screen, .color = ColorBlack, .font = FontSecondary};
    pthread_mutex_lock(&gui_lock);
    if(active && active->enabled && active->draw) active->draw(&canvas, active->draw_ctx);
    memcpy(fb, screen, sizeof(screen));
    pthread_mutex_unlock(&gui_lock);
}

void shim_input(InputKey key, InputType type) {
    pthread_mutex_lock(&gui_lock);
    ViewPort* vp = active;
    pthread_mutex_unlock(&gui_lock);
    if(!vp || !vp->input) return;
    InputEvent ev = {.sequence = ++input_sequence, .key = key, .type = type};
    vp->input(&ev, vp->input_ctx);
}
