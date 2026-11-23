/* A tiny JSON writer. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char* buf;
    size_t len, cap;
    bool comma; /* a value was written at the current level */
} Json;

void json_init(Json* j);
char* json_finish(Json* j); /* returns the string; the caller frees it */

void json_obj(Json* j); /* { */
void json_end_obj(Json* j); /* } */
void json_arr(Json* j); /* [ */
void json_end_arr(Json* j); /* ] */
void json_key(Json* j, const char* key);
void json_str(Json* j, const char* s);
void json_int(Json* j, long long v);
void json_bool(Json* j, bool v);
void json_null(Json* j);

/* Shorthands for "key": value inside an object. */
void json_kstr(Json* j, const char* key, const char* s);
void json_kint(Json* j, const char* key, long long v);
void json_kbool(Json* j, const char* key, bool v);
