#pragma once

// Network Credentials
#define SECRET_WIFI_SSID "TOVIR"
#define SECRET_WIFI_PASS "8988984646"

// Charger Identity
#define SECRET_CHARGER_ID "250822008C06"
#define SECRET_CHARGER_MODEL "Rivot Charger"
#define SECRET_CHARGER_VENDOR "Rivot Motors"

// Server Configuration (Plain WS)
#define SECRET_CSMS_HOST "ocpp.rivotmotors.com"
#define SECRET_CSMS_PORT 8080
#define SECRET_CSMS_URL "ws://ocpp.rivotmotors.com:8080/steve/websocket/CentralSystemService/" SECRET_CHARGER_ID

// Alternative URLs for testing
// #define SECRET_CSMS_URL "ws://ocpp.rivotmotors.com:9000/steve/websocket/CentralSystemService/RIVOT_100A_01"
// #define SECRET_CSMS_URL "ws://192.168.1.100:8080/steve/websocket/CentralSystemService/RIVOT_100A_01"
// #define SECRET_CSMS_URL "ws://localhost:8080/steve/websocket/CentralSystemService/RIVOT_100A_01"