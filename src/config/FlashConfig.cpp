#include "config/FlashConfig.h"
#include <string.h>

// Namespace used in flash storage
static const char* CONFIG_NS = "rivot_config";

FlashConfig::FlashConfig(IFlash& flashDevice) 
    : flash(flashDevice), dirty(false) {
    memset(&cache, 0, sizeof(cache));
}

bool FlashConfig::init() {
    if (!flash.open(CONFIG_NS)) {
        loadDefaults();
        return false;
    }

    if (!flash.getString("wifiSsid", cache.wifiSsid, sizeof(cache.wifiSsid))) {
        strcpy(cache.wifiSsid, "");
    }
    if (!flash.getString("wifiPass", cache.wifiPass, sizeof(cache.wifiPass))) {
        strcpy(cache.wifiPass, "");
    }
    if (!flash.getString("gsmApn", cache.gsmApn, sizeof(cache.gsmApn))) {
        strcpy(cache.gsmApn, "airtelgprs.com");
    }
    if (!flash.getString("csmsUrl", cache.csmsUrl, sizeof(cache.csmsUrl))) {
        strcpy(cache.csmsUrl, "ws://ocpp.rivotmotors.com:8080/steve/websocket/CentralSystemService/");
    }
    if (!flash.getString("authKey", cache.authKey, sizeof(cache.authKey))) {
        strcpy(cache.authKey, "");
    }
    
    cache.chargeLimitA = flash.getInt("chargeLimitA", 50);

    flash.close();
    dirty = false;
    return true;
}

bool FlashConfig::save() {
    if (!dirty) return true;

    if (!flash.open(CONFIG_NS)) return false;

    flash.putString("wifiSsid", cache.wifiSsid);
    flash.putString("wifiPass", cache.wifiPass);
    flash.putString("gsmApn", cache.gsmApn);
    flash.putString("csmsUrl", cache.csmsUrl);
    flash.putString("authKey", cache.authKey);
    flash.putInt("chargeLimitA", cache.chargeLimitA);

    flash.close();
    dirty = false;
    return true;
}

void FlashConfig::reset() {
    loadDefaults();
    if (flash.open(CONFIG_NS)) {
        flash.clear();
        flash.close();
    }
    dirty = true;
    save();
}

void FlashConfig::loadDefaults() {
    strcpy(cache.wifiSsid, "");
    strcpy(cache.wifiPass, "");
    strcpy(cache.gsmApn, "airtelgprs.com");
    strcpy(cache.csmsUrl, "ws://ocpp.rivotmotors.com:8080/steve/websocket/CentralSystemService/");
    strcpy(cache.authKey, "");
    cache.chargeLimitA = 50;
    dirty = true;
}

// ------ Getters ------

const char* FlashConfig::getString(const char* key, const char* defaultVal) {
    if (strcmp(key, "wifiSsid") == 0) return cache.wifiSsid;
    if (strcmp(key, "wifiPass") == 0) return cache.wifiPass;
    if (strcmp(key, "gsmApn") == 0) return cache.gsmApn;
    if (strcmp(key, "csmsUrl") == 0) return cache.csmsUrl;
    if (strcmp(key, "authKey") == 0) return cache.authKey;
    return defaultVal;
}

int FlashConfig::getInt(const char* key, int defaultVal) {
    if (strcmp(key, "chargeLimitA") == 0) return cache.chargeLimitA;
    return defaultVal;
}

float FlashConfig::getFloat(const char* key, float defaultVal) {
    return defaultVal; // No float configs currently cached
}

bool FlashConfig::getBool(const char* key, bool defaultVal) {
    return defaultVal; // No bool configs currently cached
}

// ------ Setters ------

void FlashConfig::setString(const char* key, const char* value) {
    if (strcmp(key, "wifiSsid") == 0) {
        strncpy(cache.wifiSsid, value, sizeof(cache.wifiSsid) - 1);
        dirty = true;
    } else if (strcmp(key, "wifiPass") == 0) {
        strncpy(cache.wifiPass, value, sizeof(cache.wifiPass) - 1);
        dirty = true;
    } else if (strcmp(key, "gsmApn") == 0) {
        strncpy(cache.gsmApn, value, sizeof(cache.gsmApn) - 1);
        dirty = true;
    } else if (strcmp(key, "csmsUrl") == 0) {
        strncpy(cache.csmsUrl, value, sizeof(cache.csmsUrl) - 1);
        dirty = true;
    } else if (strcmp(key, "authKey") == 0) {
        strncpy(cache.authKey, value, sizeof(cache.authKey) - 1);
        dirty = true;
    }
}

void FlashConfig::setInt(const char* key, int value) {
    if (strcmp(key, "chargeLimitA") == 0) {
        cache.chargeLimitA = value;
        dirty = true;
    }
}

void FlashConfig::setFloat(const char* key, float value) {
    // Unused
}

void FlashConfig::setBool(const char* key, bool value) {
    // Unused
}

bool FlashConfig::isDirty() {
    return dirty;
}
