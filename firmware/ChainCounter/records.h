/*
 * records.h  -  Datenstrukturen der EEPROM-Records
 *
 * Groessen muessen Vielfache von 16 Byte (ECC-Wort M95P08) und Teiler von 512 sein.
 * Bei Aenderungen am Aufbau die MAGIC-Werte in config.h aendern, damit alte
 * Records nicht falsch interpretiert werden.
 */
#pragma once
#include <stdint.h>

// Variable Daten: werden bei jedem Start, periodisch, bei reboot und bei Stromausfall geschrieben.
// (Records von FW 1.0.0 haben totalRuntime = 0 im reservierten Feld und bleiben lesbar.)
struct CounterRecord {
    uint32_t magic;          // MAGIC_COUNTER
    uint32_t seq;            // Sequenznummer
    int32_t  revolutions;    // Umdrehungszaehler
    uint32_t systemStarts;   // Systemstarts (Power Cycles)
    uint32_t totalRuntime;   // TotalRuntime [s] seit Inbetriebnahme / Factory Reset
    uint32_t reserved[2];    // = 0
    uint32_t crc;            // CRC32 ueber Byte 0..27
};
static_assert(sizeof(CounterRecord) == 32, "CounterRecord muss 32 Byte gross sein");

// Konstante, konfigurierbare Daten: werden nur bei Aenderung geschrieben.
struct ConfigRecord {
    uint32_t magic;          // MAGIC_CONFIG
    uint32_t seq;            // Sequenznummer
    float    kFactor;        // Umrechnungsfaktor [m/Umdrehung]
    uint16_t debounceMs;     // Entprellzeit Impulse [ms]
    uint16_t udpPort;        // UDP Port (Rx und Tx)
    char     ssid[33];       // WLAN SSID, nullterminiert
    char     password[65];   // WLAN Passwort, nullterminiert
    uint8_t  reserved[10];   // = 0
    uint32_t crc;            // CRC32 ueber Byte 0..123
};
static_assert(sizeof(ConfigRecord) == 128, "ConfigRecord muss 128 Byte gross sein");
