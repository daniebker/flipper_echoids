/* Line parser, kept free of SDK dependencies so it can be tested on a PC. */
#include "scanner.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int hexval(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Finds the first "xx:xx:xx:xx:xx:xx" (or '-' separated) MAC in a string. */
static bool find_mac(const char* s, uint8_t mac[6]) {
    size_t len = strlen(s);
    for(size_t i = 0; i + 17 <= len; i++) {
        bool ok = true;
        for(int b = 0; b < 6 && ok; b++) {
            const char* p = s + i + b * 3;
            int hi = hexval(p[0]), lo = hexval(p[1]);
            if(hi < 0 || lo < 0) ok = false;
            if(ok && b < 5 && p[2] != ':' && p[2] != '-') ok = false;
            if(ok) mac[b] = (uint8_t)(hi << 4 | lo);
        }
        if(ok) return true;
    }
    return false;
}

static void copy_name(char* dst, const char* src) {
    while(*src == ' ') src++;
    size_t n = 0;
    while(src[n] && src[n] != '\r' && src[n] != '\n' && n < SCAN_NAME_LEN - 1) {
        dst[n] = isprint((unsigned char)src[n]) ? src[n] : '?';
        n++;
    }
    while(n > 0 && dst[n - 1] == ' ') n--;
    dst[n] = '\0';
}

static int8_t parse_rssi(const char* line) {
    const char* p = strstr(line, "RSSI:");
    if(!p) p = strstr(line, "rssi:");
    if(!p) return -80;
    long v = strtol(p + 5, NULL, 10);
    if(v > 0 || v < -127) return -80;
    return (int8_t)v;
}

/* Derives a stable pseudo-MAC from a device name when no MAC was printed. */
static void mac_from_name(const char* name, uint8_t mac[6]) {
    uint32_t h1 = 2166136261u, h2 = 0x811C9DC5u ^ 0x5A5A5A5Au;
    for(const char* p = name; *p; p++) {
        h1 = (h1 ^ (uint8_t)*p) * 16777619u;
        h2 = (h2 ^ (uint8_t)*p) * 0x01000193u + 7u;
    }
    mac[0] = 0x02; /* locally administered */
    mac[1] = (uint8_t)(h1 >> 24);
    mac[2] = (uint8_t)(h1 >> 16);
    mac[3] = (uint8_t)(h1 >> 8);
    mac[4] = (uint8_t)h1;
    mac[5] = (uint8_t)h2;
}

bool scanner_parse_line(const char* line, ScanPhase phase, ScanEvent* out) {
    memset(out, 0, sizeof(*out));
    while(*line == ' ' || *line == '>' || *line == '#') line++;

    /* Simple CSV protocol for custom firmware:  AP,<mac>,<rssi>,<ssid>  /  BT,<mac>,<rssi>,<name> */
    if((strncmp(line, "AP,", 3) == 0 || strncmp(line, "BT,", 3) == 0) && find_mac(line, out->mac)) {
        out->type = line[0] == 'A' ? ScanEventAp : ScanEventBt;
        const char* p = strchr(line + 3, ',');
        if(p) {
            out->rssi = (int8_t)strtol(p + 1, NULL, 10);
            p = strchr(p + 1, ',');
            if(p) copy_name(out->name, p + 1);
        }
        return true;
    }

    bool has_mac = find_mac(line, out->mac);
    out->rssi = parse_rssi(line);

    if(phase == ScanPhaseWifi) {
        /* Marauder scanap: "RSSI: -60 Ch: 6 BSSID: aa:bb:cc:dd:ee:ff ESSID: MyNet" */
        if(!has_mac) return false;
        out->type = ScanEventAp;
        const char* e = strstr(line, "ESSID:");
        if(e) copy_name(out->name, e + 6);
        if(!out->name[0]) strcpy(out->name, "<hidden>");
        return true;
    }

    if(phase == ScanPhaseBt) {
        /* Marauder sniffbt prints "Device: <name or mac>" (format varies by version). */
        const char* d = strstr(line, "Device:");
        if(!has_mac) {
            if(!d) return false;
            char name[SCAN_NAME_LEN];
            copy_name(name, d + 7);
            if(!name[0]) return false;
            mac_from_name(name, out->mac);
            strcpy(out->name, name);
        } else if(d) {
            copy_name(out->name, d + 7);
        }
        out->type = ScanEventBt;
        return true;
    }
    return false;
}
