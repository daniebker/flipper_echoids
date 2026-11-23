/* PC stand-in for the Flipper SDK's ViewDispatcher: just enough for the
 * game's password prompt. See shim_text_input.c. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <gui/gui.h>

typedef struct View View;
typedef struct ViewDispatcher ViewDispatcher;
typedef bool (*ViewDispatcherNavigationEventCallback)(void* context);

typedef enum {
    ViewDispatcherTypeDesktop,
    ViewDispatcherTypeWindow,
    ViewDispatcherTypeFullscreen,
} ViewDispatcherType;

ViewDispatcher* view_dispatcher_alloc(void);
void view_dispatcher_free(ViewDispatcher* view_dispatcher);
void view_dispatcher_add_view(ViewDispatcher* view_dispatcher, uint32_t view_id, View* view);
void view_dispatcher_remove_view(ViewDispatcher* view_dispatcher, uint32_t view_id);
void view_dispatcher_switch_to_view(ViewDispatcher* view_dispatcher, uint32_t view_id);
void view_dispatcher_set_event_callback_context(ViewDispatcher* view_dispatcher, void* context);
void view_dispatcher_set_navigation_event_callback(
    ViewDispatcher* view_dispatcher,
    ViewDispatcherNavigationEventCallback callback);
void view_dispatcher_attach_to_gui(ViewDispatcher* view_dispatcher, Gui* gui, ViewDispatcherType type);
void view_dispatcher_run(ViewDispatcher* view_dispatcher);
void view_dispatcher_stop(ViewDispatcher* view_dispatcher);
