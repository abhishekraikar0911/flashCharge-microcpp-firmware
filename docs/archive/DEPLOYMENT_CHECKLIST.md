# PRODUCTION DEPLOYMENT CHECKLIST - v2.6.0

## Pre-Deployment Verification

### Code Review
- [x] All critical security fixes merged
- [x] Buffer overflow vulnerabilities eliminated
- [x] Input validation implemented
- [x] Race conditions fixed
- [x] Error handling comprehensive
- [ ] Code review completed by senior developer
- [ ] Security audit completed (if required)

### Build Verification
```bash
# Clean build
pio run --target clean
pio run -e charger_esp32_production

# Verify no warnings
# Expected output: "SUCCESS" with 0 warnings
```

---

## Phase 1: Development Environment Testing

### 1.1 Flash New Firmware
```bash
# Backup current firmware (if needed)
esptool.py --port COM3 read_flash 0x0 0x400000 backup_v2.5.1.bin

# Flash v2.6.0
pio run -e charger_esp32_production --target upload
```

### 1.2 First Boot Configuration
```cpp
// Add to setup() temporarily for credential migration
void migrateCredentials() {
    Serial.println("[MIGRATION] Starting credential migration...");
    
    // Initialize secure storage
    if (!SecureCredentials::g_secureCredentials.init()) {
        Serial.println("[MIGRATION] ❌ Failed to init secure storage!");
        return;
    }
    
    // Migrate WiFi credentials
    SecureCredentials::g_secureCredentials.storeWiFiCredentials(
        WIFI_SSID_1, WIFI_PASS_1
    );
    SecureCredentials::g_secureCredentials.storeWiFiCredentials(
        WIFI_SSID_2, WIFI_PASS_2
    );
    SecureCredentials::g_secureCredentials.storeWiFiCredentials(
        WIFI_SSID_3, WIFI_PASS_3
    );
    
    // Migrate OCPP credentials
    SecureCredentials::g_secureCredentials.storeOCPPCredentials(
        SECRET_CSMS_HOST, SECRET_CSMS_PORT, SECRET_CHARGER_ID
    );
    
    Serial.println("[MIGRATION] ✅ Credentials migrated successfully");
    Serial.println("[MIGRATION] ⚠️  Remove migrateCredentials() call and reflash");
}

// In setup(), call once:
migrateCredentials();
```

### 1.3 Verify Migration
- [ ] Serial log shows "Credentials migrated successfully"
- [ ] WiFi connects using stored credentials
- [ ] OCPP connects using stored credentials
- [ ] No credentials visible in serial logs

### 1.4 Remove Migration Code
```cpp
// Remove migrateCredentials() function
// Remove migrateCredentials() call from setup()
// Reflash firmware
pio run --target upload
```

### 1.5 Verify Secure Operation
- [ ] Device boots and connects to WiFi
- [ ] OCPP connection established
- [ ] No credentials in serial output
- [ ] No buffer overflow warnings
- [ ] No mutex timeout warnings

---

## Phase 2: Security Hardening

### 2.1 Remove Hardcoded Credentials
```bash
# Backup secrets.h
cp include/secrets.h include/secrets.h.backup

# Remove from git
git rm include/secrets.h

# Add to .gitignore
echo "include/secrets.h" >> .gitignore
echo "include/secrets.h.backup" >> .gitignore

# Commit changes
git add .gitignore
git commit -m "Security: Remove hardcoded credentials"
```

### 2.2 Enable Flash Encryption
```bash
# Generate encryption key
esptool.py generate_flash_encryption_key flash_encryption_key.bin

# IMPORTANT: Store this key securely! If lost, device cannot be updated!
# Recommended: Store in password manager + offline backup

# Burn key to device (ONE-TIME, IRREVERSIBLE!)
esptool.py --port COM3 burn_key flash_encryption flash_encryption_key.bin

# Verify encryption enabled
esptool.py --port COM3 summary
# Should show: "Flash encryption: Enabled"
```

### 2.3 Enable WSS/TLS
```cpp
// In main.cpp, replace insecure mode with:
const char* ROOT_CA = 
"-----BEGIN CERTIFICATE-----\n"
"MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh\n"
// ... (your root CA certificate)
"-----END CERTIFICATE-----\n";

g_securityManager.loadRootCA(ROOT_CA);
g_securityManager.enableCertificateVerification();

// Remove this line:
// Serial.println("[System] ⚠️  Using insecure mode for WSS");
```

### 2.4 Disable Debug Logging
```cpp
// In include/config/version.h
#define DEBUG_LEVEL 0  // 0=OFF, 1=ERROR only

// In include/debug_logger.h
#define ENABLE_DEBUG_MENU 0  // Disable debug menu in production
```

---

## Phase 3: Testing & Validation

### 3.1 Functional Testing
- [ ] WiFi connection (all 3 priority networks)
- [ ] OCPP connection (WSS)
- [ ] RemoteStartTransaction
- [ ] RemoteStopTransaction
- [ ] MeterValues transmission
- [ ] StatusNotification
- [ ] Heartbeat
- [ ] Transaction persistence (power loss test)

### 3.2 Security Testing
- [ ] No credentials in serial logs
- [ ] No credentials in flash dump (if encryption enabled)
- [ ] WSS certificate validation working
- [ ] Invalid CAN messages rejected
- [ ] Buffer overflow attempts handled safely

### 3.3 Stress Testing
- [ ] 24-hour soak test (no crashes)
- [ ] 100+ charge cycles
- [ ] Network disconnect/reconnect cycles
- [ ] CAN bus fault injection
- [ ] Power loss during charging
- [ ] Concurrent OCPP commands

### 3.4 Performance Testing
- [ ] Boot time < 10 seconds
- [ ] CAN latency < 10ms
- [ ] OCPP latency < 500ms
- [ ] Memory usage stable (no leaks)
- [ ] CPU usage < 30% average

---

## Phase 4: Production Deployment

### 4.1 Pre-Deployment Checklist
- [ ] All tests passed
- [ ] Security hardening complete
- [ ] Documentation updated
- [ ] Rollback plan prepared
- [ ] Support team briefed
- [ ] Monitoring configured

### 4.2 Deployment Steps
```bash
# 1. Flash production firmware
pio run -e charger_esp32_production --target upload

# 2. Verify boot
pio device monitor --baud 115200
# Check for: "✅ All systems initialized!"

# 3. Provision credentials (first boot only)
# Use serial console or provisioning app

# 4. Verify operation
# Check WiFi, OCPP, CAN bus status

# 5. Enable monitoring
# Configure remote logging/telemetry
```

### 4.3 Post-Deployment Verification
- [ ] Device online and reporting
- [ ] OCPP connection stable
- [ ] CAN bus communication healthy
- [ ] No error messages in logs
- [ ] Charging cycles working
- [ ] Remote commands working

---

## Phase 5: Monitoring & Maintenance

### 5.1 Monitoring Setup
```cpp
// Enable production telemetry
#define ENABLE_TELEMETRY 1
#define TELEMETRY_INTERVAL_MS 60000  // 1 minute

// Monitor these metrics:
// - Uptime
// - WiFi RSSI
// - OCPP connection status
// - CAN bus error counters
// - Memory usage
// - Reboot count
```

### 5.2 Alert Thresholds
- **Critical**: Device offline > 5 minutes
- **Warning**: WiFi RSSI < -80 dBm
- **Warning**: CAN error rate > 1%
- **Warning**: Memory usage > 90%
- **Critical**: Reboot count > 3 in 1 hour

### 5.3 Maintenance Schedule
- **Daily**: Check device status dashboard
- **Weekly**: Review error logs
- **Monthly**: Analyze performance metrics
- **Quarterly**: Security audit
- **Annually**: Firmware update cycle

---

## Rollback Procedure

### If Issues Occur

#### Option 1: Rollback to v2.5.1
```bash
# Flash backup firmware
esptool.py --port COM3 write_flash 0x0 backup_v2.5.1.bin

# Clear NVS (if needed)
pio run --target erase
```

#### Option 2: Factory Reset
```bash
# Erase entire flash
esptool.py --port COM3 erase_flash

# Reflash v2.5.1
pio run -e charger_esp32_production --target upload
```

#### Option 3: Remote Recovery (if OTA enabled)
```cpp
// Trigger OTA rollback
g_otaManager.rollback();
```

---

## Troubleshooting

### Issue: Credentials Not Working
**Symptoms**: WiFi/OCPP connection fails after migration

**Solution**:
```cpp
// Check if credentials stored
if (!SecureCredentials::g_secureCredentials.hasCredentials()) {
    Serial.println("Credentials not found - run migration again");
}

// Verify credentials (without logging values)
char ssid[33];
char pass[64];
if (SecureCredentials::g_secureCredentials.getWiFiCredentials(
    ssid, pass, sizeof(ssid), sizeof(pass))) {
    Serial.println("Credentials retrieved successfully");
} else {
    Serial.println("Failed to retrieve credentials");
}
```

### Issue: Flash Encryption Prevents Updates
**Symptoms**: Cannot flash new firmware after enabling encryption

**Solution**:
```bash
# Use encrypted OTA updates instead
# OR disable encryption (requires full erase - LOSES ALL DATA!)
esptool.py --port COM3 erase_flash
```

### Issue: Validation Rejecting Valid Data
**Symptoms**: "Invalid message" warnings for legitimate CAN data

**Solution**:
```cpp
// Adjust validation thresholds in can_validator.h
// Example: Increase max voltage from 100V to 120V
inline bool validateVoltage(float voltage) {
    return (voltage >= 0.0f && voltage <= 120.0f);  // Increased
}
```

---

## Success Criteria

### Deployment Successful If:
- ✅ All devices online and reporting
- ✅ Zero security vulnerabilities
- ✅ Zero crashes in 24 hours
- ✅ All OCPP commands working
- ✅ CAN bus communication stable
- ✅ No credential leaks
- ✅ Performance within targets

### Deployment Failed If:
- ❌ Any device offline > 5 minutes
- ❌ Security vulnerability discovered
- ❌ Crash or reboot loop
- ❌ OCPP commands failing
- ❌ CAN bus errors > 1%
- ❌ Credentials visible in logs
- ❌ Performance degraded > 20%

---

## Sign-Off

### Development Team
- [ ] Code complete and tested
- [ ] Security review passed
- [ ] Documentation complete

**Signed**: _________________ Date: _________

### QA Team
- [ ] All tests passed
- [ ] No critical bugs
- [ ] Performance acceptable

**Signed**: _________________ Date: _________

### Security Team
- [ ] Security audit passed
- [ ] Vulnerabilities resolved
- [ ] Compliance verified

**Signed**: _________________ Date: _________

### Management Approval
- [ ] Ready for production
- [ ] Risks understood
- [ ] Rollback plan approved

**Signed**: _________________ Date: _________

---

**Version**: 2.6.0  
**Deployment Date**: __________  
**Deployed By**: __________  
**Status**: ⬜ Pending / ⬜ In Progress / ⬜ Complete / ⬜ Rolled Back
