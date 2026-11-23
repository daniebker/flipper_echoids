#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put(Json* j, const char* s, size_t n) {
    if(j->len + n + 1 > j->cap) {
        size_t cap = j->cap ? j->cap : 256;
        while(cap < j->len + n + 1)
            cap *= 2;
        j->buf = realloc(j->buf, cap);
        j->cap = cap;
    }
    memcpy(j->buf + j->len, s, n);
    j->len += n;
    j->buf[j->len] = '\0';
}

static void puts_(Json* j, const char* s) {
    put(j, s, strlen(s));
}

static void sep(Json* j) {
    if(j->comma) puts_(j, ",");
    j->comma = true;
}

void json_init(Json* j) {
    memset(j, 0, sizeof(*j));
    put(j, "", 0);
}

char* json_finish(Json* j) {
    return j->buf;
}

void json_obj(Json* j) {
    sep(j);
    puts_(j, "{");
    j->comma = false;
}

void json_end_obj(Json* j) {
    puts_(j, "}");
    j->comma = true;
}

void json_arr(Json* j) {
    sep(j);
    puts_(j, "[");
    j->comma = false;
}

void json_end_arr(Json* j) {
    puts_(j, "]");
    j->comma = true;
}

static void str_raw(Json* j, const char* s);

void json_key(Json* j, const char* key) {
    sep(j);
    str_raw(j, key);
    puts_(j, ":");
    j->comma = false;
}

void json_str(Json* j, const char* s) {
    sep(j);
    str_raw(j, s);
}

static void str_raw(Json* j, const char* s) {
    puts_(j, "\"");
    for(const unsigned char* p = (const unsigned char*)s; p && *p; p++) {
        char esc[8];
        if(*p == '"' || *p == '\\') {
            esc[0] = '\\';
            esc[1] = (char)*p;
            put(j, esc, 2);
        } else if(*p < 0x20) {
            snprintf(esc, sizeof(esc), "\\u%04x", *p);
            puts_(j, esc);
        } else {
            put(j, (const char*)p, 1);
        }
    }
    puts_(j, "\"");
}

void json_int(Json* j, long long v) {
    char b[32];
    snprintf(b, sizeof(b), "%lld", v);
    sep(j);
    puts_(j, b);
}

void json_bool(Json* j, bool v) {
    sep(j);
    puts_(j, v ? "true" : "false");
}

void json_null(Json* j) {
    sep(j);
    puts_(j, "null");
}

void json_kstr(Json* j, const char* key, const char* s) {
    json_key(j, key);
    json_str(j, s);
}

void json_kint(Json* j, const char* key, long long v) {
    json_key(j, key);
    json_int(j, v);
}

void json_kbool(Json* j, const char* key, bool v) {
    json_key(j, key);
    json_bool(j, v);
}
