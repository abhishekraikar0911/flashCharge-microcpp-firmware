# flashCharge EV Charger Firmware
**Rivot Motors | OCPP 1.6 | ESP32 | v2.7.0**

A production-ready, ultra-stable ESP32 firmware for an OCPP 1.6 compliant EV charging station. Built with FreeRTOS, it features seamless CAN bus integration, TLS-encrypted GSM communication, and accurate energy metering.

## 🚀 Key Features

- **OCPP 1.6 Compliant:** Secure WebSocket (WSS) telemetry and transaction management.
- **Robust CAN Bus:** Streamlined communication with BMS & Charger hardware (Extracts Voltage, Current, and SOC purely from `0x1806E5F4`).
- **Dynamic Telemetry:** `VehicleInfo` syncs perfectly with OCPP state transitions (`Preparing`, `Charging`, `Finishing`).
- **Smart Range Calculation:** Real-time Ah-to-km range estimation tailored for Rivot Classic, Pro, and Max models.
- **Local & Remote Auth:** Dual support for physical Start/Stop buttons and cloud-based RemoteStart.
- **Auto-Recovery:** Auto-reconnect for GSM networks and persistence caching to resume charging after power loss.

## 📋 Hardware Specs

- **MCU:** ESP32 (Dual-core, 240MHz)
- **Modem:** SIMCom A7670C (GSM/LTE)
- **CAN:** MCP2515 via SPI (250kbps)
- **Power:** 5V via external supply

## 🔨 Quick Start

Ensure you have [PlatformIO](https://platformio.org/) installed, then run:

```bash
# Compile and flash to the ESP32
pio run -e charger_esp32_production --target upload

# Monitor serial logs (115200 baud)
pio device monitor --baud 115200
