/*
 * config.h  -  Chain Counter (P2410-01)
 *
 * Alle anpassbaren Werte an einer Stelle:
 *   - Defaultwerte (Erststart / Factory Reset)
 *   - Pinbelegung XIAO ESP32C3
 *   - Signalpolaritaeten
 *   - EEPROM-Layout
 *   - Grenzwerte und Timings
 */
#pragma once
#include <Arduino.h>

// ============================================================================
// Firmware-Version
// ============================================================================
#define FW_VERSION                  "1.3.0"
#define SPEC_VERSION                "1.8"

// ============================================================================
// NMEA-Adressfelder (herstellereigene Saetze: 'P' + Kuerzel 'XIS' + Typ)
// ============================================================================
#define NMEA_TX_ADDR                "PXISWND"   // Daten, 1x pro Sekunde
#define NMEA_CMD_ADDR               "PXISWNS"   // Kommandos
// Uebergangsphase: alte Kommandoform weiterhin annehmen, damit App und
// Display auch vor ihrem Update funktionieren. Spaeter entfernen.
#define NMEA_CMD_ADDR_LEGACY        "ISLWNS"

// ============================================================================
// Defaultwerte  (werden geladen, wenn das EEPROM keine gueltigen Daten hat)
// ============================================================================
#define DEFAULT_REVOLUTION_COUNTER  0               // Umdrehungszaehler  [integer]
#define DEFAULT_K_FACTOR_M          0.4f            // Umrechnungsfaktor  [m/Umdrehung]
#define DEFAULT_DEBOUNCE_MS         10              // DebounceTimer      [ms]
#define DEFAULT_SYSTEM_STARTS       0               // Systemstarts       [integer]
#define DEFAULT_TOTAL_RUNTIME       0               // TotalRuntime       [u32, s]
#define DEFAULT_UDP_PORT            1458            // UDP_Port           [integer]

// WLAN_SSID / WLAN_Passwort stehen in secrets.h (nicht im Git-Repo).
// Vorlage: secrets.h.example nach secrets.h kopieren und anpassen.
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef DEFAULT_WLAN_SSID
#warning "secrets.h fehlt: Platzhalter fuer WLAN SSID/Passwort aktiv"
#define DEFAULT_WLAN_SSID           "your_ssid"     // WLAN_SSID
#define DEFAULT_WLAN_PASSWORD       "your_password" // WLAN_Passwort
#endif

// ============================================================================
// Pinbelegung (GPIO-Nummern des ESP32-C3, siehe Schema P2410-01.401)
// ============================================================================
#define PIN_VCC_ANALOGUE    2    // GPIO2  / A0 / D0   Analog In, Teiler 82k/8k2
#define PIN_LED             3    // GPIO3  / A1 / D1   Digital Out, High = LED an
//                          4    // GPIO4  / A2 / D2   nicht angeschlossen
#define PIN_VCC_SENSE       5    // GPIO5  / A3 / D3   Digital In, Low = Speisung faellt aus
#define PIN_DIRECTION       6    // GPIO6  / D4 / SDA  Digital In
#define PIN_IMPULSE         7    // GPIO7  / D5 / SCL  Digital In, Reedkontakt
#define PIN_EE_WP_N        21    // GPIO21 / D6 / TX   !WP1  (Low aktiv)
#define PIN_EE_CS_N        20    // GPIO20 / D7 / RX   !CS1  (Low aktiv)
#define PIN_EE_SCK          8    // GPIO8  / D8 / SCK
#define PIN_EE_MISO         9    // GPIO9  / D9 / MISO (Eingang am ESP, Q des EEPROM)
#define PIN_EE_MOSI        10    // GPIO10 / D10/ MOSI

// ============================================================================
// Signalpolaritaeten (Pegel am GPIO-Pin des XIAO)
// Achtung: U2A/U2B (74LVC2G14) invertieren gegenueber den Klemmen J2.
// ============================================================================
#define DIRECTION_DOWN_LEVEL   HIGH  // High = Kette runter -> Zaehler +1 ; Low = hoch -> -1
#define IMPULSE_ACTIVE_LEVEL   HIGH  // High = Reedkontakt geschlossen
#define VCC_SENSE_OK_LEVEL     HIGH  // High = Speisung OK

// ============================================================================
// Spannungsmessung VCC_Analogue
// ============================================================================
#define VCC_DIVIDER_RATIO      ((82.0f + 8.2f) / 8.2f)  // = 11.0  (R1 82k, R2 8k2)
#define VCC_CALIBRATION        1.000f   // Feinabgleich, nach Messung anpassen
#define VCC_ADC_SAMPLES        16       // Mittelwert ueber n Messungen

// ============================================================================
// Grenzwerte fuer Kommandos
// ============================================================================
#define DEBOUNCE_MS_MIN        1
#define DEBOUNCE_MS_MAX        1000
#define K_FACTOR_MIN           0.001f
#define K_FACTOR_MAX           100.0f
#define DIRECTION_DEBOUNCE_MS  20       // feste Entprellung fuer Direction

// ============================================================================
// Timings
// ============================================================================
#define SERIAL_BAUD                 115200
#define UDP_TX_INTERVAL_MS          1000    // NMEA-Satz 1x pro Sekunde
#define SAVE_INTERVAL_MIN           15      // periodische Speicherung der variablen Daten [min]
#define WIFI_RECONNECT_INTERVAL_MS  15000   // neuer Verbindungsversuch nach x ms
#define VCC_SENSE_ARM_DELAY_MS      200     // VCC_Sense muss x ms High sein, bevor
                                            // die Ausfallerkennung scharf geschaltet wird
#define HOSTNAME                    "chaincounter"

// ============================================================================
// EEPROM M95P08 (8 Mbit = 1 MByte, Page 512 Byte, ECC-Wort 16 Byte)
// ============================================================================
#define EE_SPI_CLOCK_HZ     10000000UL
#define EE_PAGE_SIZE        512UL
#define EE_SECTOR_SIZE      4096UL
#define EE_BLOCK_SIZE       65536UL

// Ringpuffer fuer Zaehler-Records (32 Byte):  64 KB = 128 Pages = 2048 Records
#define EE_COUNTER_BASE     0x000000UL
#define EE_COUNTER_SIZE     0x010000UL
// Ringpuffer fuer Konfigurations-Records (128 Byte):  4 KB = 8 Pages = 32 Records
#define EE_CONFIG_BASE      0x010000UL
#define EE_CONFIG_SIZE      0x001000UL

#define MAGIC_COUNTER       0x434E5452UL   // "CNTR"
#define MAGIC_CONFIG        0x43464731UL   // "CFG1"
