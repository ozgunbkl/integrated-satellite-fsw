#include "utils.h"

uint16_t utils_crc16(const uint8_t *data, size_t len)
{
    if (data == NULL && len != 0) {
        return 0;
    }
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (unsigned bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U)
                                 : (uint16_t)(crc << 1);
        }
    }
    return crc;
}
