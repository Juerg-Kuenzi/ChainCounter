/*
 * m95p.cpp  -  Treiber fuer ST M95P08 (SPI Page EEPROM)
 */
#include "m95p.h"

// Instruktionen
static const uint8_t CMD_WREN  = 0x06;
static const uint8_t CMD_RDSR  = 0x05;
static const uint8_t CMD_READ  = 0x03;
static const uint8_t CMD_RDID  = 0x9F;
static const uint8_t CMD_PGWR  = 0x02;   // Page Write   (Erase + Program)
static const uint8_t CMD_PGPR  = 0x0A;   // Page Program (nur geloeschte Zellen)
static const uint8_t CMD_PGER  = 0xDB;   // Page Erase   512 B
static const uint8_t CMD_SCER  = 0x20;   // Sector Erase 4 KB
static const uint8_t CMD_BKER  = 0xD8;   // Block Erase  64 KB

static const uint8_t SR_WIP = 0x01;      // Write In Progress
static const uint8_t SR_WEL = 0x02;      // Write Enable Latch

// Timeouts (grosszuegig ueber den Datenblatt-Maximalwerten)
static const uint32_t TO_PGWR_US = 10000;    // typ 2.3 ms, max 6 ms
static const uint32_t TO_PGPR_US = 5000;     // typ 1.2 ms (512 B), max 1.5 ms
static const uint32_t TO_PGER_US = 10000;    // max 4.5 ms
static const uint32_t TO_SCER_US = 200000;
static const uint32_t TO_BKER_US = 3000000;

bool M95P::begin()
{
    pinMode(PIN_EE_CS_N, OUTPUT);
    digitalWrite(PIN_EE_CS_N, HIGH);
    pinMode(PIN_EE_WP_N, OUTPUT);
    digitalWrite(PIN_EE_WP_N, LOW);          // Schreibschutz aktiv
    SPI.begin(PIN_EE_SCK, PIN_EE_MISO, PIN_EE_MOSI, -1);
    delay(2);                                // Power-up Zeit EEPROM

    uint32_t id = readJedecId();
    return (id != 0x000000UL) && (id != 0xFFFFFFUL);
}

void M95P::select()
{
    SPI.beginTransaction(_spi);
    digitalWrite(PIN_EE_CS_N, LOW);
}

void M95P::deselect()
{
    digitalWrite(PIN_EE_CS_N, HIGH);
    SPI.endTransaction();
}

void M95P::sendAddr(uint32_t addr)
{
    SPI.transfer((uint8_t)(addr >> 16));
    SPI.transfer((uint8_t)(addr >> 8));
    SPI.transfer((uint8_t)(addr));
}

uint32_t M95P::readJedecId()
{
    select();
    SPI.transfer(CMD_RDID);
    uint32_t id = (uint32_t)SPI.transfer(0xFF) << 16;
    id |= (uint32_t)SPI.transfer(0xFF) << 8;
    id |= SPI.transfer(0xFF);
    deselect();
    return id;
}

uint8_t M95P::readStatus()
{
    select();
    SPI.transfer(CMD_RDSR);
    uint8_t sr = SPI.transfer(0xFF);
    deselect();
    return sr;
}

void M95P::read(uint32_t addr, void *buf, size_t len)
{
    uint8_t *p = static_cast<uint8_t *>(buf);
    memset(p, 0xFF, len);
    select();
    SPI.transfer(CMD_READ);
    sendAddr(addr);
    SPI.transfer(p, len);                    // in-place: sendet 0xFF, empfaengt Daten
    deselect();
}

bool M95P::writeEnable()
{
    select();
    SPI.transfer(CMD_WREN);
    deselect();
    return (readStatus() & SR_WEL) != 0;
}

bool M95P::waitReady(uint32_t timeoutUs, bool fast)
{
    uint32_t t0 = micros();
    for (;;) {
        if ((readStatus() & SR_WIP) == 0) return true;
        if ((uint32_t)(micros() - t0) > timeoutUs) return false;
        if (!fast) {
            if (timeoutUs > 20000) vTaskDelay(1);
            else delayMicroseconds(50);
        }
    }
}

bool M95P::writeCmd(uint8_t cmd, uint32_t addr, const void *buf, size_t len,
                    uint32_t timeoutUs, bool fast)
{
    digitalWrite(PIN_EE_WP_N, HIGH);         // Schreibschutz aufheben
    bool ok = writeEnable();
    if (ok) {
        select();
        SPI.transfer(cmd);
        sendAddr(addr);
        if (buf && len) SPI.writeBytes(static_cast<const uint8_t *>(buf), len);
        deselect();
        ok = waitReady(timeoutUs, fast);
    }
    digitalWrite(PIN_EE_WP_N, LOW);          // Schreibschutz wieder aktiv
    return ok;
}

static bool crossesPage(uint32_t addr, size_t len)
{
    return len == 0 || ((addr % EE_PAGE_SIZE) + len) > EE_PAGE_SIZE;
}

bool M95P::pageWrite(uint32_t addr, const void *buf, size_t len, bool fast)
{
    if (crossesPage(addr, len)) return false;
    return writeCmd(CMD_PGWR, addr, buf, len, TO_PGWR_US, fast);
}

bool M95P::pageProgram(uint32_t addr, const void *buf, size_t len, bool fast)
{
    if (crossesPage(addr, len)) return false;
    return writeCmd(CMD_PGPR, addr, buf, len, TO_PGPR_US, fast);
}

bool M95P::pageErase(uint32_t addr)
{
    return writeCmd(CMD_PGER, addr & ~(EE_PAGE_SIZE - 1), nullptr, 0, TO_PGER_US, false);
}

bool M95P::sectorErase(uint32_t addr)
{
    return writeCmd(CMD_SCER, addr & ~(EE_SECTOR_SIZE - 1), nullptr, 0, TO_SCER_US, false);
}

bool M95P::blockErase(uint32_t addr)
{
    return writeCmd(CMD_BKER, addr & ~(EE_BLOCK_SIZE - 1), nullptr, 0, TO_BKER_US, false);
}
