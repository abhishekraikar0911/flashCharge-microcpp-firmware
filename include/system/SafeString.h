#ifndef SAFE_STRING_H
#define SAFE_STRING_H

#include <Arduino.h>
#include <cstring>

/**
 * @file safe_string.h
 * @brief Memory-safe string operations to prevent buffer overflows
 */

namespace SafeString
{
    /**
     * @brief Safe string copy with guaranteed null termination
     * @param dest Destination buffer
     * @param src Source string
     * @param destSize Size of destination buffer
     * @return Number of characters copied (excluding null terminator)
     */
    inline size_t copy(char *dest, const char *src, size_t destSize)
    {
        if (!dest || !src || destSize == 0)
            return 0;

        size_t i = 0;
        while (i < destSize - 1 && src[i] != '\0')
        {
            dest[i] = src[i];
            i++;
        }
        dest[i] = '\0';
        return i;
    }

    /**
     * @brief Safe formatted print with bounds checking
     * @param buffer Destination buffer
     * @param bufferSize Size of destination buffer
     * @param format Printf-style format string
     * @return Number of characters written (excluding null terminator)
     */
    inline int format(char *buffer, size_t bufferSize, const char *format, ...)
    {
        if (!buffer || bufferSize == 0)
            return 0;

        va_list args;
        va_start(args, format);
        int result = vsnprintf(buffer, bufferSize, format, args);
        va_end(args);

        // Ensure null termination even if truncated
        buffer[bufferSize - 1] = '\0';
        return (result >= 0 && result < (int)bufferSize) ? result : (int)bufferSize - 1;
    }

    /**
     * @brief Validate string is null-terminated within bounds
     * @param str String to validate
     * @param maxLen Maximum length to check
     * @return true if valid, false otherwise
     */
    inline bool validate(const char *str, size_t maxLen)
    {
        if (!str)
            return false;

        for (size_t i = 0; i < maxLen; i++)
        {
            if (str[i] == '\0')
                return true;
        }
        return false;
    }

    /**
     * @brief Safe string concatenation
     * @param dest Destination buffer
     * @param src Source string to append
     * @param destSize Total size of destination buffer
     * @return true if successful, false if truncated
     */
    inline bool append(char *dest, const char *src, size_t destSize)
    {
        if (!dest || !src || destSize == 0)
            return false;

        size_t destLen = strnlen(dest, destSize);
        if (destLen >= destSize - 1)
            return false;

        size_t remaining = destSize - destLen - 1;
        size_t srcLen = strnlen(src, remaining + 1);

        if (srcLen > remaining)
        {
            memcpy(dest + destLen, src, remaining);
            dest[destSize - 1] = '\0';
            return false;
        }

        memcpy(dest + destLen, src, srcLen + 1);
        return true;
    }
}

#endif // SAFE_STRING_H
