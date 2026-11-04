/*
 * nmea.h  -  NMEA0183 Hilfsfunktionen
 */
#pragma once
#include <Arduino.h>

#define NMEA_MAX_FIELDS  8

// XOR-Checksumme ueber 'len' Zeichen (ohne '$' und '*').
uint8_t nmeaChecksum(const char *s, size_t len);

// Haengt "*XX\r\n" an einen Satz an, der mit '$' beginnt. false = Puffer zu klein.
bool nmeaAppendChecksum(char *buf, size_t size);

// Prueft Format "$feld0,feld1,...*xx" und Checksumme und zerlegt den Satz.
// 'line' wird dabei veraendert. fields[0] = Adressfeld (z.B. "ISLWNS").
// Bei Fehler: false und *err zeigt auf eine Fehlerbeschreibung.
bool nmeaParse(char *line, char **fields, int maxFields, int *count, const char **err);

// Strikte Zahlen-Parser: ganzer String muss eine gueltige Zahl sein.
bool parseIntStrict(const char *s, long *out);
bool parseFloatStrict(const char *s, float *out);
