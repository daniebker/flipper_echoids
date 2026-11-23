#pragma once

#include "canvas.h"

void elements_progress_bar(Canvas* canvas, int32_t x, int32_t y, size_t width, float progress);
void elements_scrollbar_pos(Canvas* canvas, int32_t x, int32_t y, size_t height, size_t pos, size_t total);
void elements_scrollbar(Canvas* canvas, size_t pos, size_t total);
void elements_button_center(Canvas* canvas, const char* str);
