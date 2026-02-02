#!/usr/bin/env python3
"""Upload firmware to SteVe and trigger OTA update"""
import requests
import json
from datetime import datetime, timedelta

# Configuration
STEVE_URL = "https://your-steve-server.com"  # Change this
STEVE_USER = "admin"
STEVE_PASS = "admin"
CHARGER_ID = "RIVOT_100A_01"
FIRMWARE_PATH = "../.pio/build/charger_esp32_production/firmware.bin"

def upload_firmware():
    """Upload firmware binary to SteVe server"""
    print(f"[1/3] Uploading {FIRMWARE_PATH} to SteVe...")
    
    with open(FIRMWARE_PATH, 'rb') as f:
        files = {'file': ('firmware.bin', f, 'application/octet-stream')}
        response = requests.post(
            f"{STEVE_URL}/steve/manager/firmware/upload",
            files=files,
            auth=(STEVE_USER, STEVE_PASS),
            verify=False  # For self-signed certs
        )
    
    if response.status_code == 200:
        firmware_url = response.json().get('url')
        print(f"✅ Uploaded: {firmware_url}")
        return firmware_url
    else:
        print(f"❌ Upload failed: {response.status_code}")
        return None

def trigger_update(firmware_url):
    """Send UpdateFirmware command to charger"""
    print(f"[2/3] Sending UpdateFirmware to {CHARGER_ID}...")
    
    retrieve_date = (datetime.utcnow() + timedelta(minutes=2)).isoformat() + "Z"
    
    payload = {
        "chargeBoxId": CHARGER_ID,
        "location": firmware_url,
        "retrieveDate": retrieve_date,
        "retries": 3,
        "retryInterval": 60
    }
    
    response = requests.post(
        f"{STEVE_URL}/steve/api/v1/updateFirmware",
        json=payload,
        auth=(STEVE_USER, STEVE_PASS),
        verify=False
    )
    
    if response.status_code == 200:
        print(f"✅ UpdateFirmware sent")
        print(f"📅 Scheduled for: {retrieve_date}")
        return True
    else:
        print(f"❌ Failed: {response.status_code}")
        return False

def check_status():
    """Check firmware update status"""
    print(f"[3/3] Checking status...")
    
    response = requests.get(
        f"{STEVE_URL}/steve/api/v1/chargePoints/{CHARGER_ID}/firmwareStatus",
        auth=(STEVE_USER, STEVE_PASS),
        verify=False
    )
    
    if response.status_code == 200:
        status = response.json().get('status')
        print(f"📊 Status: {status}")
    else:
        print(f"⚠️  Status check failed")

if __name__ == "__main__":
    print("🚀 ESP32 OTA Update via SteVe\n")
    
    # Upload firmware
    firmware_url = upload_firmware()
    if not firmware_url:
        exit(1)
    
    # Trigger update
    if not trigger_update(firmware_url):
        exit(1)
    
    # Check status
    check_status()
    
    print("\n✅ Done! Monitor ESP32 serial output for OTA progress.")
