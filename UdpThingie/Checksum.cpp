#include "pch.h"
#include "Checksum.h"

/**
 * @brief Computes the CRC computation table
*/
Checksum::Checksum()
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;

        for (uint32_t j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
        }

        this->crcTable[i] = c;
    }
}

uint32_t Checksum::crc32(void *_buf, size_t len)
{
    unsigned char* buf = reinterpret_cast<unsigned char*>(_buf);
    uint32_t c = 0xFFFFFFFF;

    for (size_t i = 0; i < len; i++) {
        c = this->crcTable[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    }

    return c ^ 0xFFFFFFFF;
}