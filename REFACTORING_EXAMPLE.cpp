// ============================================================================
// PRACTICAL REFACTORING EXAMPLE - CAN Recovery Code
// ============================================================================
// This file shows EXACTLY how to refactor your existing CAN recovery logs
// Copy-paste the "AFTER" code to replace your current implementation
// ============================================================================

// ----------------------------------------------------------------------------
// BEFORE (Current messy code)
// ----------------------------------------------------------------------------
/*
Serial.println("[CAN] 🚨 BUS-OFF detected, initiating recovery...");
Serial.println("[CAN_RECOVERY] Step 1: Deinitializing TWAI driver...");
// ... deinit code ...
Serial.println("[CAN_RECOVERY] ✅ Deinit successful");
Serial.println("[CAN_RECOVERY] Step 2: Reinitializing TWAI driver...");
Serial.println("[CAN1] Initializing TWAI...");
// ... init code ...
Serial.println("[CAN1] ✅ TWAI initialized successfully");
Serial.println("[CAN_RECOVERY] ✅ Reinit successful");
Serial.println("[CAN_RECOVERY] Step 3: Disabling charging for safety...");
// ... disable code ...
Serial.println("[CAN_RECOVERY] ✅ Charging disabled");
Serial.println("[CAN_RECOVERY] Step 4: Marking for re-initialization...");
// ... mark code ...
Serial.println("[CAN_RECOVERY] 🔄 Recovery sequence complete");
Serial.println();
Serial.println("[CHARGER] Re-sending initialization sequence after recovery...");
*/

// ----------------------------------------------------------------------------
// AFTER (Clean structured code)
// ----------------------------------------------------------------------------

#include "utils/log_macros.h"
#include "utils/can_status_logger.h"

// At the start of recovery
LOG_SECTION_START("CAN BUS RECOVERY");
LOG_CRITICAL(CAN, "Bus-off detected, initiating recovery");

// Step 1
CANStatusLogger::printRecoveryStep(1, 4, "Deinitializing TWAI driver");
// ... deinit code ...
LOG_INFO(CAN, "Deinit successful");

// Step 2
CANStatusLogger::printRecoveryStep(2, 4, "Reinitializing TWAI driver");
// ... init code ...
LOG_INFO(CAN, "TWAI initialized successfully");

// Step 3
CANStatusLogger::printRecoveryStep(3, 4, "Disabling charging for safety");
// ... disable code ...
LOG_INFO(CAN, "Charging disabled");

// Step 4
CANStatusLogger::printRecoveryStep(4, 4, "Marking for re-initialization");
// ... mark code ...

CANStatusLogger::printRecoveryComplete(true);
LOG_INFO(CHARGER, "Re-sending initialization sequence");
LOG_SECTION_END();

// ============================================================================
// OUTPUT COMPARISON
// ============================================================================

/*
BEFORE OUTPUT (Messy):
[CAN] 🚨 BUS-OFF detected, initiating recovery...
[CAN_RECOVERY] Step 1: Deinitializing TWAI driver...
[CAN_RECOVERY] ✅ Deinit successful
[CAN_RECOVERY] Step 2: Reinitializing TWAI driver...
[CAN1] Initializing TWAI...
[CAN1] ✅ TWAI initialized successfully
[CAN_RECOVERY] ✅ Reinit successful
[CAN_RECOVERY] Step 3: Disabling charging for safety...
[CAN_RECOVERY] ✅ Charging disabled
[CAN_RECOVERY] Step 4: Marking for re-initialization...
[CAN_RECOVERY] 🔄 Recovery sequence complete

[CHARGER] Re-sending initialization sequence after recovery...

AFTER OUTPUT (Clean):
╔═══════════════════════════════════════════════════════════════╗
║  CAN BUS RECOVERY                                              ║
╚═══════════════════════════════════════════════════════════════╝
[CAN] 🚨 Bus-off detected, initiating recovery
[CAN] ℹ️  Recovery 1/4: Deinitializing TWAI driver
[CAN] ℹ️  Deinit successful
[CAN] ℹ️  Recovery 2/4: Reinitializing TWAI driver
[CAN] ℹ️  TWAI initialized successfully
[CAN] ℹ️  Recovery 3/4: Disabling charging for safety
[CAN] ℹ️  Charging disabled
[CAN] ℹ️  Recovery 4/4: Marking for re-initialization
[CAN] ℹ️  Recovery sequence completed successfully
[CHRG] ℹ️  Re-sending initialization sequence
╚═══════════════════════════════════════════════════════════════╝
*/

// ============================================================================
// MORE EXAMPLES FROM YOUR CODE
// ============================================================================

// ----------------------------------------------------------------------------
// Example 2: CAN Status Report
// ----------------------------------------------------------------------------

// BEFORE:
/*
Serial.println("╔═════════════════════════════════════════════════════════╗");
Serial.println("║  CAN BUS STATUS REPORT (Every 10s)                            ║");
Serial.println("╚═════════════════════════════════════════════════════════╝");
Serial.printf("[CAN_STATUS] State: %d (0=STOPPED 1=RUNNING 2=BUS_OFF 3=RECOVERING)\n", status.state);
Serial.printf("[CAN_STATUS] TX Errors: %d | RX Errors: %d\n", status.tx_error_counter, status.rx_error_counter);
Serial.printf("[CAN_STATUS] TX Failed: %d | RX Missed: %d\n", status.tx_failed_count, status.rx_missed_count);
Serial.printf("[CAN_STATUS] Bus Errors: %d | Arb Lost: %d\n", status.bus_error_count, status.arb_lost_count);
Serial.printf("[CAN_STATUS] TX Queue: %d | RX Queue: %d\n", status.msgs_to_tx, status.msgs_to_rx);
Serial.println("╚═════════════════════════════════════════════════════════╝");
*/

// AFTER:
twai_status_info_t status;
twai_get_status_info(&status);
CANStatusLogger::printStatusReport(status);

// ----------------------------------------------------------------------------
// Example 3: OCPP Vehicle Info
// ----------------------------------------------------------------------------

// BEFORE:
/*
Serial.printf("[OCPP] 📤 Sending VehicleInfo (Pre-Tx):\n");
Serial.printf("  SOC=%.1f%% | Model=%s | Range=%.1fkm | MaxI=%.1fA | VIN=%s\n", 
              soc, model, range, maxI, vin);
*/

// AFTER:
OCPPStatusLogger::printVehicleInfoSent(soc, model, range, maxI, vin);

// ----------------------------------------------------------------------------
// Example 4: System Status
// ----------------------------------------------------------------------------

// BEFORE:
/*
Serial.printf("[Status] Uptime: %us | WiFi: %s | OCPP: %s | State: %s\n",
              uptime, wifiConnected ? "✅" : "❌", 
              ocppConnected ? "Connected" : "Disconnected", state);
Serial.printf("[Metrics] V=%.1fV I=%.1fA SOC=%.1f%% Range=%.1fkm Temp=%.1f°C Energy=%.2fWh\n",
              voltage, current, soc, range, temp, energy);
*/

// AFTER:
OCPPStatusLogger::printSystemStatus(uptime, wifiConnected, ocppConnected, state, txActive, charging);
OCPPStatusLogger::printVehicleMetrics(voltage, current, soc, range, temp, energy, model);

// ============================================================================
// QUICK MIGRATION CHECKLIST
// ============================================================================
/*
□ 1. Add includes to your file:
     #include "utils/log_macros.h"
     #include "utils/can_status_logger.h"
     #include "utils/ocpp_status_logger.h"

□ 2. Find CAN recovery code and replace with structured version

□ 3. Find CAN status report and replace with CANStatusLogger::printStatusReport()

□ 4. Find OCPP vehicle info and replace with OCPPStatusLogger::printVehicleInfoSent()

□ 5. Find system status and replace with OCPPStatusLogger::printSystemStatus()

□ 6. Test in serial monitor

□ 7. Commit changes

DONE! Your logs are now clean and professional! 🎉
*/
