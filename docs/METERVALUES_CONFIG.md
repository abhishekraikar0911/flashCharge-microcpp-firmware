# MeterValues Configuration

## Overview
ESP32 OCPP charger sends 7 standard OCPP 1.6 measurands every 30 seconds during active transactions.

## Configuration
```cpp
// MeterValues interval: 30 seconds
MeterValueSampleInterval = 30

// Enabled measurands
MeterValuesSampledData = "Energy.Active.Import.Register,Power.Active.Import,SoC,Current.Offered,Temperature,Voltage,Current.Import"
```

## Measurands Sent

| Measurand | Unit | Source | Description |
|-----------|------|--------|-------------|
| Energy.Active.Import.Register | Wh | `energyWh` | Total energy consumed |
| Power.Active.Import | W | `terminalVolt × terminalCurr` | Real-time power |
| SoC | Percent | `socPercent` | Battery state of charge |
| Current.Offered | A | `BMS_Imax` | Max current BMS allows |
| Temperature | Celsius | `chargerTemp` | Charger temperature |
| Voltage | V | `terminalVolt` | Terminal voltage (CAN 0x00433F01) |
| Current.Import | A | `terminalCurr` | Actual current flowing (CAN 0x00433F01) |

## Example MeterValues Message
```json
{
  "connectorId": 1,
  "transactionId": 93,
  "meterValue": [{
    "timestamp": "2026-01-20T09:34:32Z",
    "sampledValue": [
      {"value": "1", "measurand": "Energy.Active.Import.Register", "unit": "Wh"},
      {"value": "142.00", "measurand": "Power.Active.Import", "unit": "W"},
      {"value": "84.77", "measurand": "SoC", "unit": "Percent"},
      {"value": "2.00", "measurand": "Current.Offered", "unit": "A"},
      {"value": "27.65", "measurand": "Temperature", "unit": "Celsius"},
      {"value": "77.19", "measurand": "Voltage", "unit": "V"},
      {"value": "1.84", "measurand": "Current.Import", "unit": "A"}
    ]
  }]
}
```

## Key Points
- Uses **terminal values** (real measurements from CAN) for accurate metering
- Power calculated as `terminalVolt × terminalCurr` (not charger setpoints)
- All measurands are standard OCPP 1.6 (no custom names)
- Automatically sent by MicroOcpp library during transactions
