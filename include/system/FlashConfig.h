/**
 * @file FlashConfig.h
 * @brief Concrete System Layer implementation of IConfig
 * @layer System
 *
 * Uses dependent injection of an IFlash object to persist settings.
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include "system/IConfig.h"
#include "hal/interfaces/IFlash.h"

class FlashConfig : public IConfig {
public:
    /** Constructor requires underlying generic flash HAL */
    FlashConfig(IFlash& flashDevice);
    virtual ~FlashConfig() = default;

    bool init() override;
    bool save() override;
    void reset() override;

    const char* getString(const char* key, const char* defaultVal = "") override;
    int         getInt(const char* key, int defaultVal = 0) override;
    float       getFloat(const char* key, float defaultVal = 0.0f) override;
    bool        getBool(const char* key, bool defaultVal = false) override;

    void setString(const char* key, const char* value) override;
    void setInt(const char* key, int value) override;
    void setFloat(const char* key, float value) override;
    void setBool(const char* key, bool value) override;

    bool isDirty() override;

private:
    struct ConfigCache {
        // Essential cached credentials / config
        char wifiSsid[33];
        char wifiPass[65];
        char gsmApn[33];
        char csmsUrl[129];
        char authKey[33];
        int chargeLimitA;
    };

    IFlash& flash;
    ConfigCache cache;
    bool dirty;

    void loadDefaults();
};
