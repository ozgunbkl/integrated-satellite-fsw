#ifndef ARCHIVE_UTILS_H
#define ARCHIVE_UTILS_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection/xorout.
 * Empty data returns 0xFFFF; NULL with nonzero length is invalid and returns 0.
 */
uint16_t utils_crc16(const uint8_t *data, size_t len);

#endif
