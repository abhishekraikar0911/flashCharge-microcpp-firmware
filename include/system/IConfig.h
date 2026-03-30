/**
 * @file IConfig.h
 * @brief System layer interface for application configuration
 * @layer System
 *
 * Implementations: FlashConfig (uses IFlash)
 * Provides typed key-value storage for the application. Replaces direct NVS/Preferences usage.
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include <stddef.h>

class IConfig {
public:
    virtual ~IConfig() = default;

    /** Load configuration from underlying storage to RAM cache */
    virtual bool init() = 0;

    /** Flush dirty changes from RAM cache to underlying storage */
    virtual bool save() = 0;

    /** Reset configuration to factory defaults */
    virtual void reset() = 0;

    // Typed Getters
    virtual const char* getString(const char* key, const char* defaultVal = "") = 0;
    virtual int         getInt(const char* key, int defaultVal = 0) = 0;
    virtual float       getFloat(const char* key, float defaultVal = 0.0f) = 0;
    virtual bool        getBool(const char* key, bool defaultVal = false) = 0;

    // Typed Setters (marks dirty)
    virtual void setString(const char* key, const char* value) = 0;
    virtual void setInt(const char* key, int value) = 0;
    virtual void setFloat(const char* key, float value) = 0;
    virtual void setBool(const char* key, bool value) = 0;

    /** @return true if there are unsaved changes in the RAM cache */
    virtual bool isDirty() = 0;
};
