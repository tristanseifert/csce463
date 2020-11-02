#ifndef CHECKSUM_H
#define CHECKSUM_H

#include <cstdint>

/**
 * @brief Implements CRC32
*/
class Checksum {
public:
    Checksum();

    virtual uint32_t crc32(void* buf, size_t len);

private:
    uint32_t crcTable[256];
};

#endif