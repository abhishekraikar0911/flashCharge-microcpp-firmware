#pragma once
#include "Arduino.h"

// Minimal stub for WiFi.h to allow native compilation of network_manager
class WiFiClass {
public:
    void begin(const char* ssid, const char* pass) {}
    int status() { return 0; } // WL_IDLE_STATUS
    void disconnect(bool wifioff = false, bool eraseap = false) {}
    void mode(int m) {}
};

extern WiFiClass WiFi;

#define WL_CONNECTED 3
#define WIFI_STA 1
