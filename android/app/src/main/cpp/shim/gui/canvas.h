#pragma once

#include <stddef.h>
#include <stdint.h>

#define CANVAS_WIDTH  128
#define CANVAS_HEIGHT 64

typedef enum {
    ColorWhite = 0,
    ColorBlack = 1,
    ColorXOR = 2,
} Color;

typedef enum {
    FontPrimary,
    FontSecondary,
    FontKeyboard,
    FontBigNumbers,
    FontTotalNumber,
} Font;

typedef enum {
    AlignLeft,
    AlignRight,
    AlignTop,
    AlignBottom,
    AlignCenter,
} Align;

typedef struct Canvas Canvas;

void canvas_clear(Canvas* canvas);
void canvas_set_color(Canvas* canvas, Color color);
void canvas_set_font(Canvas* canvas, Font font);
void canvas_draw_dot(Canvas* canvas, int32_t x, int32_t y);
void canvas_draw_box(Canvas* canvas, int32_t x, int32_t y, size_t width, size_t height);
void canvas_draw_frame(Canvas* canvas, int32_t x, int32_t y, size_t width, size_t height);
void canvas_draw_rframe(Canvas* canvas, int32_t x, int32_t y, size_t width, size_t height, size_t radius);
void canvas_draw_line(Canvas* canvas, int32_t x1, int32_t y1, int32_t x2, int32_t y2);
void canvas_draw_circle(Canvas* canvas, int32_t x, int32_t y, size_t radius);
void canvas_draw_str(Canvas* canvas, int32_t x, int32_t y, const char* str);
void canvas_draw_str_aligned(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    Align horizontal,
    Align vertical,
    const char* str);
uint16_t canvas_string_width(Canvas* canvas, const char* str);
