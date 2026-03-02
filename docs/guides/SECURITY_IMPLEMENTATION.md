# Security Implementation Guide

## 🔐 5 Critical Security Fixes - Implementation Plan

---

## Fix 1: Remove Hardcoded Credentials

**Files Created:**
- `include/modules/secure_credentials.h`
- `src/modules/secure_credentials.cpp`
- `include/modules/provisioning.h`
- `src/modules/provisioning.cpp`

**Integration in main.cpp:**

```cpp
#include "modules/secure_credentials.h"
#include "modules/provisioning.h"

void setup() {
    Serial.begin(115200);
    SecureCredentials::init();
    
    if (Provisioning::isProvisioningRequired()) {
        Provisioning::enterProvisioningMode();
        return;
    }
    
    char ssid[64], password[64], chargerId[64], serverUrl[128];
    SecureCredentials::getWiFiCredentials(ssid, sizeof(ssid), password, sizeof(password));
    SecureCredentials::getOCPPCredentials(chargerId, sizeof(chargerId), serverUrl, sizeof(serverUrl));
    
    WiFi.begin(ssid, password);
}
```

**Usage:** First boot enters provisioning mode, credentials stored in encrypted NVS.

---

## Fix 2: Enable WSS (Secure WebSocket)

**Files Created:**
- `include/modules/wss_config.h`
- `src/modules/wss_config.cpp`

**Get CA Certificate:**
```bash
openssl s_client -showcerts -connect ocpp.rivotmotors.com:443 < /dev/null | openssl x509 -outform PEM
```

**Integration:**
```cpp
#include "modules/wss_config.h"

WSSConfig::init();
WiFiClientSecure* client = WSSConfig::createSecureClient();
mocpp_initialize(*client, "wss://ocpp.rivotmotors.com:443", 443, chargerId);
```

---

## Fix 3: Certificate Pinning

```cpp
const char* fingerprint = "AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD";
WSSConfig::validateCertificate(fingerprint);
```

---

## Fix 4: OTA Signature Verification

**Files Created:**
- `include/modules/secure_ota.h`
- `src/modules/secure_ota.cpp`

**Generate Keys:**
```bash
openssl genrsa -out firmware_private.pem 2048
openssl rsa -in firmware_private.pem -pubout -out firmware_public.pem
```

**Sign Firmware:**
```bash
pio run -e charger_esp32_production
openssl dgst -sha256 -binary .pio/build/charger_esp32_production/firmware.bin > firmware.hash
openssl rsautl -sign -inkey firmware_private.pem -in firmware.hash -out firmware.sig
```

**Integration:**
```cpp
#include "modules/secure_ota.h"

SecureOTA::init();
SecureOTA::beginUpdate(size, signature, sigLen);
SecureOTA::writeChunk(data, len);
SecureOTA::finalizeUpdate(); // Verifies signature
```

---

## Fix 5: NVS Encryption

**Generate Key:**
```bash
espsecure.py generate_flash_encryption_key nvs_key.bin
```

**Create partitions_encrypted.csv:**
```csv
nvs,      data, nvs,     0x9000,  0x5000, encrypted
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x1E0000,
app1,     app,  ota_1,   0x1F0000,0x1E0000,
spiffs,   data, spiffs,  0x3D0000,0x30000,
```

**Update platformio.ini:**
```ini
board_build.partitions = partitions_encrypted.csv
build_flags = -DCONFIG_NVS_ENCRYPTION=1
```

---

## Implementation Checklist

- [ ] Add secure_credentials module
- [ ] Add provisioning module  
- [ ] Add wss_config module
- [ ] Add secure_ota module
- [ ] Update main.cpp
- [ ] Replace WS with WSS
- [ ] Generate CA certificate
- [ ] Generate RSA keys
- [ ] Create encrypted partitions
- [ ] Test provisioning
- [ ] Test WSS connection
- [ ] Test OTA signature
- [ ] Remove secrets.h
- [ ] Update .gitignore

---

## Testing

```bash
pio run --target erase
pio run --target upload
pio device monitor
```

---

## Security Best Practices

1. Never commit: `secrets.h`, `nvs_key.bin`, `firmware_private.pem`
2. Rotate credentials every 90 days
3. Monitor failed auth attempts
4. Use HSM for production
5. Enable ESP32 secure boot

---

**Last Updated**: January 2025
