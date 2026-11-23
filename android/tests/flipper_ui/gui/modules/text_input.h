/* PC stand-in for the Flipper SDK's TextInput. See shim_text_input.c. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "../view_dispatcher.h"

typedef struct TextInput TextInput;
typedef void (*TextInputCallback)(void* context);

TextInput* text_input_alloc(void);
void text_input_free(TextInput* text_input);
View* text_input_get_view(TextInput* text_input);
void text_input_set_result_callback(
    TextInput* text_input,
    TextInputCallback callback,
    void* callback_context,
    char* text_buffer,
    size_t text_buffer_size,
    bool clear_default_text);
void text_input_set_minimum_length(TextInput* text_input, size_t minimum_length);
void text_input_set_header_text(TextInput* text_input, const char* text);

/* Tests: what the "player" types next (NULL = presses Back). */
void shim_text_input_answer(const char* text);
