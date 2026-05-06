# Future Implementation Roadmap

This document outlines planned improvements and technical debt resolution for the ESP32 EV Charger Firmware. These items are slated for future development cycles.

## 1. Dynamic VIN Provisioning (NVS Secure Storage)
**Current Status:** ~~The firmware hardcodes a vehicle VIN (`"ME9NP1411H2172005"`) in `OcppService.cpp` for data transfer.~~ **[COMPLETED]**
**Future Action:**
* ~~Expand the `SecureConfig` system (NVS encrypted storage) to store the VIN and other charger-specific metadata.~~ **Done** - The VIN is now read dynamically from the `config` namespace in NVS (`Preferences`). If no VIN is found, it falls back to the default `ME9NP1411H2172005`.
* This allows the exact same compiled `.bin` firmware to be flashed to hundreds of production chargers.
* Implement a provisioning flow (via Serial/Bluetooth/WebUI) to set the VIN on the factory floor without recompiling code.

## 2. State-Based Telemetry Optimization (GSM/Data Saver)
**Current Status:** The firmware relies on periodic timers to send telemetry (MeterValues) to the CSMS. Over GSM networks, this can lead to unnecessary data consumption and server spam.
**Future Action:**
* Implement "State-Based Trigger Logic" for MeterValues.
* Only push data payloads to the server on critical state transitions:
  * Entering `Preparing` state.
  * Entering `Charging` state.
  * Entering `Finishing` state.
  * Significant SOC jumps (e.g., every 5% increase).
  * Significant voltage drops (e.g., >5V drop).
* Reduces GSM data costs and prevents network congestion.

## 3. Contact Welding Detection (IEC 61851 Safety)
**Current Status:** The logic is fully written inside `SafetyService::pollContactWelding()`, but is temporarily disabled via an early `return;` statement.
**Context:** Current hardware topology measures the voltage on the battery side of the circuit, meaning the voltage doesn't decay when the DC contactor opens while the vehicle is plugged in.
**Future Action:**
* Once the hardware design is updated to place the voltage sensor *before* the contactor (measuring the DC-link capacitor), remove the `return;` statement in `SafetyService.cpp` to instantly re-enable the protection.
* The 15-second / 3.0V decay check will then actively monitor for fused contactors and safely trigger a `PowerSwitchFailure` alert.
