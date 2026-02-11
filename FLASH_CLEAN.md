# Clean Flash and Upload New Firmware

## One-Time NVS Cleanup (removes invalid txId=-1)

```bash
# Erase NVS partition only (keeps firmware)
pio run --target erase

# OR full flash erase (recommended for clean start)
pio run -e charger_esp32_production --target erase

# Then upload new firmware
pio run -e charger_esp32_production --target upload

# Monitor
pio device monitor --baud 115200
```

## Expected Output After Clean Flash

```
[PERSIST] Reboot count: 1
[PERSIST] No active transaction found
[OCPP_SM] ✅ State machine ready (no persisted transaction)
```

## If You See This - Success!

```
[OCPP_SM] ✅ Transaction started: 12345 (tag: RemoteStart)
[PERSIST] Saved transaction: 12345 (tag: RemoteStart)
```

No more "Resuming persisted transaction: -1"
