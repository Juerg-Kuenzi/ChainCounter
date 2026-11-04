/*
 * recordlog.h  -  Wear-Leveling Ringpuffer fuer Records fester Groesse im M95P08
 *
 * Aufbau jedes Records:
 *   [0..3]   magic   (Record-Typ)
 *   [4..7]   seq     (fortlaufende Sequenznummer, hoechste = aktuellster Record)
 *   [8..n-5] Nutzdaten
 *   [n-4..]  crc32   (ueber Byte 0..n-5, Startwert CRC32STARTVALUE)
 *
 * Neue Records werden immer in den naechsten Slot geschrieben (fortschreitende
 * Nutzung aller Zellen). Beim Start wird der Bereich gescannt und der gueltige
 * Record mit der hoechsten Sequenznummer verwendet.
 *
 * Der naechste Slot wird im Voraus vorbereitet (geloescht, solange die Speisung
 * gut ist). Beim Stromausfall ist dann nur ein schnelles "Page Program"
 * (~0.1-0.2 ms) noetig, kein Suchen und kein Loeschen.
 *
 * Recordgroesse: Vielfaches von 16 Byte (ECC-Wort), Teiler von 512 (Page).
 */
#pragma once
#include <Arduino.h>
#include "m95p.h"

class RecordLog {
public:
    RecordLog(M95P &ee, uint32_t base, uint32_t size, uint16_t recSize, uint32_t magic);

    // Bereich scannen. true = gueltiger Record gefunden und nach 'rec' kopiert.
    bool scan(void *rec);

    // Naechsten Slot vorbereiten (blank pruefen, Page ggf. loeschen).
    // Nur bei guter Speisung aufrufen (kann bis ~5 ms dauern).
    bool prepareNext();

    // Record anhaengen. Setzt magic, seq und crc in 'rec'.
    // fast = true: Busy-Polling (Stromausfall-Pfad).
    bool append(void *rec, bool fast = false);

    // Ganzen Bereich loeschen (Factory Reset).
    bool eraseAll();

    bool     nextReady() const { return _nextReady; }
    uint32_t lastSeq()   const { return _lastSeq; }
    int32_t  lastSlot()  const { return _lastSlot; }
    uint32_t slotCount() const { return _slots; }
    uint32_t slotAddr(uint32_t slot) const { return _base + slot * _recSize; }

private:
    bool isValid(const uint8_t *rec) const;
    bool isBlank(uint32_t addr, uint32_t len);

    M95P    &_ee;
    uint32_t _base;
    uint32_t _size;
    uint16_t _recSize;
    uint32_t _magic;
    uint32_t _slots;
    uint32_t _slotsPerPage;

    int32_t  _lastSlot  = -1;   // -1 = noch kein Record vorhanden
    uint32_t _lastSeq   = 0;
    uint32_t _nextSlot  = 0;
    bool     _nextReady = false;
};
