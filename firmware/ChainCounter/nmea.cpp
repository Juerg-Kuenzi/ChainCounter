/*
 * nmea.cpp  -  NMEA0183 Hilfsfunktionen
 */
#include "nmea.h"
#include <errno.h>
#include <math.h>
#include <ctype.h>

uint8_t nmeaChecksum(const char *s, size_t len)
{
    uint8_t cs = 0;
    while (len--) cs ^= (uint8_t)*s++;
    return cs;
}

bool nmeaAppendChecksum(char *buf, size_t size)
{
    size_t len = strlen(buf);
    if (len < 2 || buf[0] != '$' || len + 6 > size) return false;
    uint8_t cs = nmeaChecksum(buf + 1, len - 1);
    snprintf(buf + len, size - len, "*%02X\r\n", cs);
    return true;
}

bool nmeaParse(char *line, char **fields, int maxFields, int *count, const char **err)
{
    static char msg[64];
    *count = 0;

    size_t len = strlen(line);
    if (len < 4 || line[0] != '$') { *err = "Satz beginnt nicht mit '$'"; return false; }

    for (size_t i = 0; i < len; i++) {
        if (line[i] < 0x20 || line[i] > 0x7E) { *err = "ungueltiges Zeichen im Satz"; return false; }
    }

    char *star = strchr(line, '*');
    if (!star)                                   { *err = "Checksumme '*xx' fehlt"; return false; }
    if (strlen(star + 1) != 2 ||
        !isxdigit((unsigned char)star[1]) ||
        !isxdigit((unsigned char)star[2]))       { *err = "Checksumme muss genau 2 Hex-Zeichen haben"; return false; }

    uint8_t want = (uint8_t)strtoul(star + 1, nullptr, 16);
    uint8_t have = nmeaChecksum(line + 1, (size_t)(star - (line + 1)));
    if (want != have) {
        snprintf(msg, sizeof(msg), "Checksumme falsch (empfangen %02X, berechnet %02X)", want, have);
        *err = msg;
        return false;
    }
    *star = '\0';

    int n = 0;
    char *p = line + 1;
    for (;;) {
        if (n >= maxFields) { *err = "zu viele Felder"; return false; }
        fields[n++] = p;
        char *c = strchr(p, ',');
        if (!c) break;
        *c = '\0';
        p = c + 1;
    }
    *count = n;
    return true;
}

bool parseIntStrict(const char *s, long *out)
{
    const char *d = (*s == '+' || *s == '-') ? s + 1 : s;
    if (!isdigit((unsigned char)*d)) return false;
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || *end != '\0') return false;
    *out = v;
    return true;
}

bool parseFloatStrict(const char *s, float *out)
{
    const char *d = (*s == '+' || *s == '-') ? s + 1 : s;
    if (!isdigit((unsigned char)*d) && *d != '.') return false;
    char *end;
    errno = 0;
    float v = strtof(s, &end);
    if (errno != 0 || *end != '\0' || end == s || !isfinite(v)) return false;
    *out = v;
    return true;
}
