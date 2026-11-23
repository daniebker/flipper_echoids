/* Hooks between the platform layer (JNI / test driver) and the game. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "input/input.h"

/* Setup, before the game starts. */
void shim_set_data_dir(const char* dir);
void shim_set_device(const uint8_t mac[6], const char* name);

/* Renders the current screen into a 128x64 framebuffer (1 = black pixel). */
void shim_render(uint8_t fb[64][128]);
/* True if the game asked for a redraw since the last render. */
bool shim_take_dirty(void);
/* Delivers a button event to the game. */
void shim_input(InputKey key, InputType type);
/* Milliseconds of vibration requested since the last call. */
int shim_take_vibration(void);
/* The game's entry point (defined in echoids.c). */
int32_t echoids_app(void* p);
/* Ask the game to save now (e.g. when the phone app is paused). */
extern volatile int echoids_save_request;
