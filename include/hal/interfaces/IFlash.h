/**
 * @file IFlash.h
 * @brief HAL interface for non-volatile flash/NVS storage
 * @layer HAL — MCU peripheral abstraction
 *
 * Implementations: Esp32Flash (wraps Preferences.h / NVS)
 * Used by: FlashConfig (IConfig implementation in System layer)
 * Replaces all direct Preferences.h usage.
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include <stddef.h>

class IFlash {
public:
    virtual ~IFlash() = default;

    /**
     * Open a named namespace.
     * @param ns  Namespace string (<=15 chars)
     * @return true on success
     */
    virtual bool open(const char* ns) = 0;

    /** Close the current namespace. */
    virtual void close() = 0;

    /** Store a string under key. @return true on success */
    virtual bool putString(const char* key, const char* value) = 0;

    /**
     * Read a string. Writes empty string if key absent.
     * @return true if key existed
     */
    virtual bool getString(const char* key, char* buf, size_t len) = 0;

    /** Store a 32-bit signed int. @return true on success */
    virtual bool putInt(const char* key, int value) = 0;

    /** Read a 32-bit signed int. @return defaultVal if key absent. */
    virtual int  getInt(const char* key, int defaultVal = 0) = 0;

    /** Store a boolean. @return true on success */
    virtual bool putBool(const char* key, bool value) = 0;

    /** Read a boolean. @return defaultVal if key absent. */
    virtual bool getBool(const char* key, bool defaultVal = false) = 0;

    /** Store a float. @return true on success */
    virtual bool putFloat(const char* key, float value) = 0;

    /** Read a float. @return defaultVal if key absent. */
    virtual float getFloat(const char* key, float defaultVal = 0.0f) = 0;

    /** Erase all keys in the currently open namespace. */
    virtual bool clear() = 0;
};
