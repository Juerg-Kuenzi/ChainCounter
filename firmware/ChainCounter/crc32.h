/*
 * crc32.h  -  CRC32 fuer die EEPROM-Records
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef uint32_t eos_u32_t;

#define CRC32STARTVALUE 0x4711e2c7U

// Berechnet die CRC32 ueber 'len' Bytes. Optional mit Startwert (zum Verketten).
eos_u32_t crc32_calc(const void *data, size_t len, eos_u32_t crc = CRC32STARTVALUE);
