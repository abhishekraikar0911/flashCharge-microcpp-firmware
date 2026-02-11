#include "debug_logger.h"

/**
 * EXAMPLE: How to fix CAN message printing
 * 
 * This shows the EXACT changes needed in your CAN RX handlers
 */

// ============================================
// WRONG WAY (Current - prints always)
// ============================================
void can2_rx_task_WRONG(void *arg) {
    while (1) {
        // ... receive CAN message ...
        
        // ❌ WRONG - Always prints, ignores debug sections
        Serial.printf("[CAN2-TX] 0x%08X: ", msg.id);
        for (int i = 0; i < 8; i++) {
            Serial.printf("%02X ", msg.data[i]);
        }
        Serial.println();
        
        // ❌ WRONG - Always prints
        Serial.printf("[BMS] Vmax=%.1fV Imax=%.1fA\n", vmax, imax);
    }
}

// ============================================
// RIGHT WAY (Fixed - respects debug sections)
// ============================================
void can2_rx_task_CORRECT(void *arg) {
    while (1) {
        // ... receive CAN message ...
        
        // ✅ CORRECT - Only prints when BMS debug is enabled
        LOG_BMS("[CAN2-TX] 0x%08X: %02X %02X %02X %02X %02X %02X %02X %02X", 
                msg.id, 
                msg.data[0], msg.data[1], msg.data[2], msg.data[3],
                msg.data[4], msg.data[5], msg.data[6], msg.data[7]);
        
        // ✅ CORRECT - Only prints when BMS debug is enabled
        LOG_BMS("[BMS] Vmax=%.1fV Imax=%.1fA", vmax, imax);
    }
}

// ============================================
// CHARGER EXAMPLE
// ============================================
void can1_rx_task_CORRECT(void *arg) {
    while (1) {
        // ... receive CAN message ...
        
        // ✅ Use LOG_CHARGER for charger module messages
        LOG_CHARGER("[CAN1-RX] 0x%08X: %02X %02X %02X %02X %02X %02X %02X %02X", 
                    msg.id,
                    msg.data[0], msg.data[1], msg.data[2], msg.data[3],
                    msg.data[4], msg.data[5], msg.data[6], msg.data[7]);
    }
}

// ============================================
// QUICK SEARCH & REPLACE GUIDE
// ============================================
/*
 * In your main.cpp or CAN handler files:
 * 
 * 1. Find all: Serial.printf("[CAN2-
 *    Replace with: LOG_BMS("[CAN2-
 * 
 * 2. Find all: Serial.printf("[BMS]
 *    Replace with: LOG_BMS("[BMS]
 * 
 * 3. Find all: Serial.printf("[CAN1-
 *    Replace with: LOG_CHARGER("[CAN1-
 * 
 * 4. Find all: Serial.printf("[CHARGER]
 *    Replace with: LOG_CHARGER("[CHARGER]
 * 
 * 5. Find all: Serial.printf("[OCPP]
 *    Replace with: LOG_OCPP("[OCPP]
 * 
 * 6. Find all: Serial.printf("[WiFi]
 *    Replace with: LOG_WIFI("[WiFi]
 * 
 * 7. Find all: Serial.printf("[State]
 *    Replace with: LOG_STATE("[State]
 * 
 * 8. Find all: Serial.printf("[System]
 *    Replace with: LOG_SYSTEM("[System]
 * 
 * IMPORTANT: After replacing, remove any Serial.println() calls
 * that were used with these prints. The LOG_XXX macros add newlines automatically.
 */
