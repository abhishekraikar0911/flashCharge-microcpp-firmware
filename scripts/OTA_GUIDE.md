# OTA Firmware Update Guide

## Method 1: Upload to SteVe Server (Production)

### 1. Build Firmware
```bash
pio run -e charger_esp32_production
```

### 2. Sign Firmware (ECDSA P-256)
```bash
cd scripts
pip install cryptography
python sign_firmware.py --key /path/to/ota_private_key.pem
```

### 3. Upload to SteVe via API
```bash
cd scripts
python upload_to_steve.py
```

**What happens:**
1. Script uploads `firmware.signed.bin` to SteVe server via HTTPS
2. SteVe stores file and returns URL
3. Script sends `UpdateFirmware` OCPP command with URL
4. ESP32 receives command and downloads from SteVe
5. ESP32 flashes and reboots

### 3. Configure Script
Edit `upload_to_steve.py`:
```python
STEVE_URL = "https://your-steve-server.com"
STEVE_USER = "admin"
STEVE_PASS = "admin"
CHARGER_ID = "250822008C06"
```

## Method 2: Local HTTP Server (Testing)

### 1. Build Firmware
```bash
pio run -e charger_esp32_production
```
Binary: `.pio/build/charger_esp32_production/firmware.signed.bin`

### 2. Start HTTP Server
```bash
cd scripts
python serve_firmware.py
```

### 3. Send UpdateFirmware from SteVe

**SteVe Web UI:**
- Operations → UpdateFirmware
- Charge Point: `250822008C06`
- Location: `http://192.168.1.100:8000/firmware.signed.bin` (replace with your IP)
- Retrieve Date: 2 minutes from now
- Retries: 3
- Click **Update**

### 4. Monitor ESP32 Serial
```
[OTA] 📦 Starting update
[OTA] 📝 Written: 1024 bytes
[OTA] ✅ Update complete! Rebooting...
```

## Alternative: FTP Server

```bash
pip install pyftpdlib
python -m pyftpdlib -p 21 -w
```

Use in SteVe: `ftp://192.168.1.100:21/firmware.signed.bin`

## Troubleshooting

**"Download failed"**: Check firewall, ensure server is accessible from ESP32
**"Update.begin failed"**: Not enough flash space, check partition table
**"Write failed"**: Corrupted download, check network stability
