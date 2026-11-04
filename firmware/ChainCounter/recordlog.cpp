/*
 * recordlog.cpp  -  Wear-Leveling Ringpuffer im M95P08
 */
#include "recordlog.h"
#include "crc32.h"

static const uint16_t MAX_REC_SIZE = 128;

RecordLog::RecordLog(M95P &ee, uint32_t base, uint32_t size, uint16_t recSize, uint32_t magic)
    : _ee(ee), _base(base), _size(size), _recSize(recSize), _magic(magic)
{
    _slots        = size / recSize;
    _slotsPerPage = EE_PAGE_SIZE / recSize;
}

bool RecordLog::isValid(const uint8_t *rec) const
{
    uint32_t magic, crc;
    memcpy(&magic, rec, 4);
    if (magic != _magic) return false;
    memcpy(&crc, rec + _recSize - 4, 4);
    return crc32_calc(rec, _recSize - 4) == crc;
}

bool RecordLog::isBlank(uint32_t addr, uint32_t len)
{
    uint8_t buf[64];
    while (len) {
        uint32_t n = len > sizeof(buf) ? sizeof(buf) : len;
        _ee.read(addr, buf, n);
        for (uint32_t i = 0; i < n; i++) {
            if (buf[i] != 0xFF) return false;
        }
        addr += n;
        len  -= n;
    }
    return true;
}

bool RecordLog::scan(void *rec)
{
    static uint8_t page[EE_PAGE_SIZE];
    _lastSlot = -1;
    _lastSeq  = 0;

    for (uint32_t pageAddr = _base; pageAddr < _base + _size; pageAddr += EE_PAGE_SIZE) {
        _ee.read(pageAddr, page, EE_PAGE_SIZE);
        for (uint32_t i = 0; i < _slotsPerPage; i++) {
            const uint8_t *r = page + i * _recSize;
            if (!isValid(r)) continue;
            uint32_t seq;
            memcpy(&seq, r + 4, 4);
            if (_lastSlot < 0 || seq > _lastSeq) {
                _lastSeq  = seq;
                _lastSlot = (int32_t)((pageAddr - _base) / _recSize + i);
            }
        }
    }

    _nextSlot  = (_lastSlot < 0) ? 0 : (uint32_t)(_lastSlot + 1) % _slots;
    _nextReady = false;

    if (_lastSlot < 0) return false;
    _ee.read(slotAddr(_lastSlot), rec, _recSize);
    return isValid(static_cast<uint8_t *>(rec));     // nochmals pruefen
}

bool RecordLog::prepareNext()
{
    uint32_t s = _nextSlot;

    if ((s % _slotsPerPage) != 0 && !isBlank(slotAddr(s), _recSize)) {
        // Slot mitten in der Page ist nicht leer (z.B. abgebrochener Schreibvorgang):
        // auf den Anfang der naechsten Page ausweichen.
        s = ((s / _slotsPerPage) + 1) * _slotsPerPage % _slots;
    }

    if ((s % _slotsPerPage) == 0) {
        // Neue Page: komplett leer machen, damit alle Slots darin per
        // Page Program (ohne Erase) beschrieben werden koennen.
        uint32_t pageAddr = slotAddr(s);
        if (!isBlank(pageAddr, EE_PAGE_SIZE)) {
            if (!_ee.pageErase(pageAddr))           return false;
            if (!isBlank(pageAddr, EE_PAGE_SIZE))   return false;
        }
    }

    _nextSlot  = s;
    _nextReady = true;
    return true;
}

bool RecordLog::append(void *rec, bool fast)
{
    uint8_t *r = static_cast<uint8_t *>(rec);
    uint32_t seq = (_lastSlot < 0) ? 1 : _lastSeq + 1;
    memcpy(r, &_magic, 4);
    memcpy(r + 4, &seq, 4);
    uint32_t crc = crc32_calc(r, _recSize - 4);
    memcpy(r + _recSize - 4, &crc, 4);

    uint32_t addr = slotAddr(_nextSlot);
    bool ok = _nextReady ? _ee.pageProgram(addr, r, _recSize, fast)   // schnell, Slot ist leer
                         : _ee.pageWrite(addr, r, _recSize, fast);    // Fallback mit internem Erase

    if (ok && _recSize <= MAX_REC_SIZE) {                              // Verify
        uint8_t check[MAX_REC_SIZE];
        _ee.read(addr, check, _recSize);
        ok = (memcmp(check, r, _recSize) == 0);
    }

    if (ok) {
        _lastSlot = (int32_t)_nextSlot;
        _lastSeq  = seq;
    }
    _nextSlot  = (_nextSlot + 1) % _slots;   // bei Fehler: defekten Slot ueberspringen
    _nextReady = false;
    return ok;
}

bool RecordLog::eraseAll()
{
    bool ok = true;
    uint32_t addr = _base;
    while (addr < _base + _size) {
        if ((addr % EE_BLOCK_SIZE) == 0 && (_base + _size - addr) >= EE_BLOCK_SIZE) {
            ok &= _ee.blockErase(addr);
            addr += EE_BLOCK_SIZE;
        } else {
            ok &= _ee.sectorErase(addr);
            addr += EE_SECTOR_SIZE;
        }
    }
    _lastSlot  = -1;
    _lastSeq   = 0;
    _nextSlot  = 0;
    _nextReady = false;
    return ok;
}
