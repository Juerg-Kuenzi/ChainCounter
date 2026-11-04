/*
 * m95p.h  -  Treiber fuer ST M95P08 (SPI Page EEPROM, 8 Mbit)
 *
 * Verwendete Instruktionen (ST TN1559 / DS12964):
 *   06h WREN, 04h WRDI, 05h RDSR, 03h READ, 9Fh RDID
 *   02h Page Write   (1..512 Byte ueberschreiben, internes Erase+Program, max. ~6 ms)
 *   0Ah Page Program (nur auf geloeschte Bereiche, sehr schnell: ~0.1-0.2 ms fuer 32 Byte)
 *   DBh Page Erase (512 B), 20h Sector Erase (4 KB), D8h Block Erase (64 KB)
 *
 * !WP1 wird nur waehrend Schreib-/Loeschoperationen deaktiviert (High),
 * sonst ist es aktiv (Low).
 */
#pragma once
#include <Arduino.h>
#include <SPI.h>
#include "config.h"

class M95P {
public:
    bool     begin();
    uint32_t readJedecId();
    uint8_t  readStatus();

    void read(uint32_t addr, void *buf, size_t len);

    // Alle Schreibfunktionen: Daten duerfen keine Page-Grenze (512 B) ueberschreiten.
    // 'fast' = Busy-Polling ohne yield (fuer den Stromausfall-Pfad).
    bool pageWrite(uint32_t addr, const void *buf, size_t len, bool fast = false);
    bool pageProgram(uint32_t addr, const void *buf, size_t len, bool fast = false);
    bool pageErase(uint32_t addr);
    bool sectorErase(uint32_t addr);
    bool blockErase(uint32_t addr);

private:
    bool writeEnable();
    bool waitReady(uint32_t timeoutUs, bool fast);
    bool writeCmd(uint8_t cmd, uint32_t addr, const void *buf, size_t len,
                  uint32_t timeoutUs, bool fast);
    void select();
    void deselect();
    void sendAddr(uint32_t addr);

    SPISettings _spi{EE_SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0};
};
