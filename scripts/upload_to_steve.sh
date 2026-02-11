#!/bin/bash
# Quick OTA upload using curl

STEVE_URL="https://your-steve-server.com"
STEVE_USER="admin"
STEVE_PASS="admin"
CHARGER_ID="250822008C06"
FIRMWARE="../.pio/build/charger_esp32_production/firmware.signed.bin"

echo "🚀 Uploading firmware to SteVe..."

# Upload firmware
UPLOAD_RESPONSE=$(curl -s -X POST \
  -u "$STEVE_USER:$STEVE_PASS" \
  -F "file=@$FIRMWARE" \
  "$STEVE_URL/steve/manager/firmware/upload")

FIRMWARE_URL=$(echo $UPLOAD_RESPONSE | jq -r '.url')
echo "✅ Uploaded: $FIRMWARE_URL"

# Trigger update
RETRIEVE_DATE=$(date -u -d '+2 minutes' +"%Y-%m-%dT%H:%M:%SZ")

curl -X POST \
  -u "$STEVE_USER:$STEVE_PASS" \
  -H "Content-Type: application/json" \
  -d "{
    \"chargeBoxId\": \"$CHARGER_ID\",
    \"location\": \"$FIRMWARE_URL\",
    \"retrieveDate\": \"$RETRIEVE_DATE\",
    \"retries\": 3,
    \"retryInterval\": 60
  }" \
  "$STEVE_URL/steve/api/v1/updateFirmware"

echo ""
echo "✅ UpdateFirmware sent to $CHARGER_ID"
echo "📅 Scheduled: $RETRIEVE_DATE"
