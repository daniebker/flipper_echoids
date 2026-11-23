/*
 * PC stand-ins for the SDK's ViewDispatcher and TextInput: running the
 * dispatcher "types" the answer set with shim_text_input_answer() and presses
 * OK, or presses Back if there is none.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui/modules/text_input.h"

struct TextInput {
    TextInputCallback callback;
    void* context;
    char* buffer;
    size_t size;
};

struct View {
    TextInput* input;
};

struct ViewDispatcher {
    TextInput* input;
    void* context;
    ViewDispatcherNavigationEventCallback back;
    bool running;
};

static char answer[64];
static bool has_answer;

void shim_text_input_answer(const char* text) {
    has_answer = text != NULL;
    snprintf(answer, sizeof(answer), "%s", text ? text : "");
}

TextInput* text_input_alloc(void) {
    TextInput* t = calloc(1, sizeof(TextInput) + sizeof(View));
    ((View*)(t + 1))->input = t;
    return t;
}

void text_input_free(TextInput* t) {
    free(t);
}

View* text_input_get_view(TextInput* t) {
    return (View*)(t + 1);
}

void text_input_set_result_callback(TextInput* t, TextInputCallback cb, void* ctx, char* buf, size_t size, bool clear) {
    t->callback = cb;
    t->context = ctx;
    t->buffer = buf;
    t->size = size;
    if(clear && size) buf[0] = '\0';
}

void text_input_set_minimum_length(TextInput* t, size_t n) {
    (void)t;
    (void)n;
}

void text_input_set_header_text(TextInput* t, const char* text) {
    (void)t;
    (void)text;
}

ViewDispatcher* view_dispatcher_alloc(void) {
    return calloc(1, sizeof(ViewDispatcher));
}

void view_dispatcher_free(ViewDispatcher* d) {
    free(d);
}

void view_dispatcher_add_view(ViewDispatcher* d, uint32_t id, View* view) {
    (void)id;
    d->input = view->input;
}

void view_dispatcher_remove_view(ViewDispatcher* d, uint32_t id) {
    (void)id;
    d->input = NULL;
}

void view_dispatcher_switch_to_view(ViewDispatcher* d, uint32_t id) {
    (void)d;
    (void)id;
}

void view_dispatcher_set_event_callback_context(ViewDispatcher* d, void* ctx) {
    d->context = ctx;
}

void view_dispatcher_set_navigation_event_callback(ViewDispatcher* d, ViewDispatcherNavigationEventCallback cb) {
    d->back = cb;
}

void view_dispatcher_attach_to_gui(ViewDispatcher* d, Gui* gui, ViewDispatcherType type) {
    (void)d;
    (void)gui;
    (void)type;
}

void view_dispatcher_run(ViewDispatcher* d) {
    d->running = true;
    if(has_answer && d->input && d->input->callback) {
        snprintf(d->input->buffer, d->input->size, "%s", answer);
        d->input->callback(d->input->context);
    } else if(d->back) {
        d->back(d->context);
    }
    d->running = false;
}

void view_dispatcher_stop(ViewDispatcher* d) {
    d->running = false;
}
