#pragma once

#include <stdbool.h>

#include "canvas.h"
#include "../input/input.h"

typedef struct ViewPort ViewPort;
typedef struct Gui Gui;

typedef enum {
    GuiLayerDesktop,
    GuiLayerWindow,
    GuiLayerStatusBarLeft,
    GuiLayerStatusBarRight,
    GuiLayerFullscreen,
    GuiLayerMAX,
} GuiLayer;

typedef void (*ViewPortDrawCallback)(Canvas* canvas, void* context);
typedef void (*ViewPortInputCallback)(InputEvent* event, void* context);

ViewPort* view_port_alloc(void);
void view_port_free(ViewPort* view_port);
void view_port_draw_callback_set(ViewPort* view_port, ViewPortDrawCallback callback, void* context);
void view_port_input_callback_set(ViewPort* view_port, ViewPortInputCallback callback, void* context);
void view_port_update(ViewPort* view_port);
void view_port_enabled_set(ViewPort* view_port, bool enabled);

void gui_add_view_port(Gui* gui, ViewPort* view_port, GuiLayer layer);
void gui_remove_view_port(Gui* gui, ViewPort* view_port);
