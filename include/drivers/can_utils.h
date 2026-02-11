#ifndef CAN_UTILS_H
#define CAN_UTILS_H

#include <stdint.h>
#include <string.h>

/**
 * @file can_utils.h
 * @brief Shared utilities for CAN message parsing and hardware abstraction
 */

namespace can_utils
{
    /**
     * @brief Parse a 4-byte big-endian float from a buffer
     */
    static inline float parseBEFloat(const uint8_t *b)
    {
        uint8_t tmp[4] = {b[3], b[2], b[1], b[0]};
        float f;
        memcpy(&f, tmp, sizeof(f));
        return f;
    }

    /**
     * @brief Parse a 4-byte big-endian unsigned integer from a buffer
     */
    static inline uint32_t parseBEUint32(const uint8_t *b)
    {
        return (uint32_t(b[0]) << 24) |
               (uint32_t(b[1]) << 16) |
               (uint32_t(b[2]) << 8) |
               uint32_t(b[3]);
    }

    /**
     * @brief Parse a 2-byte big-endian unsigned integer from a buffer
     */
    static inline uint16_t parseBEUint16(const uint8_t *b)
    {
        return (uint16_t(b[0]) << 8) | uint16_t(b[1]);
    }
}

#endif // CAN_UTILS_H
