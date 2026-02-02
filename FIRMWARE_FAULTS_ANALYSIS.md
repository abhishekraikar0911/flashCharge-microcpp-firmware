# Firmware Faults Analysis

## Critical Issues Identified

### 1. ❌ No Transaction Lock
**Problem**: `chargingEnabled` can be set without valid OCPP transaction
**Location**: `src/modules/ocpp_manager.cpp` line 183
**Impact**: Hardware can charge without OCPP authorization

### 2. ❌ Charging Enabled Without TransactionId  
**Problem**: `chargingEnabled = true` happens before transaction validation
**Location**: `src/modules/ocpp_manager.cpp` TxNotification_StartTx callback
**Impact**: Charging starts even if transaction fails to initialize

### 3. ❌ Status Driven by Hardware Signals, Not OCPP State
**Problem**: Hardware signals (`gunPhysicallyConnected`, `batteryConnected`) directly control state
**Location**: `src/main.cpp` lines 235-285 (plug detection)
**Impact**: OCPP state machine bypassed by hardware

### 4. ❌ StopTransaction Triggered by EVDisconnected Even When TX Invalid
**Problem**: Plug disconnect triggers StopTransaction without checking transaction validity
**Location**: `src/main.cpp` plug disconnect logic
**Impact**: Spurious StopTransaction messages sent to server

### 5. ❌ State Resets to Available While EV Still Connected
**Problem**: No validation that EV is disconnected before resetting to Available
**Location**: State machine transitions
**Impact**: Premature state reset while vehicle still plugged in

---

## Required Fixes

### Fix 1: Add Transaction Lock
```cpp
// Add global transaction lock
static bool transactionLocked = false;
static int activeTransactionId = -1;

// Only allow charging if transaction is locked
if (transactionLocked && activeTransactionId > 0) {
    chargingEnabled = true;
}
```

### Fix 2: Validate Transaction Before Enabling Charging
```cpp
} else if (notification == TxNotification_StartTx) {
    // Get transaction ID from MicroOcpp
    if (tx && tx->getTransactionId() > 0) {
        activeTransactionId = tx->getTransactionId();
        transactionLocked = true;
        chargingEnabled = true;
        Serial.printf("[OCPP] ✅ Transaction %d LOCKED - Charging enabled\n", activeTransactionId);
    } else {
        Serial.println("[OCPP] ❌ Transaction invalid - NOT enabling charging");
    }
}
```

### Fix 3: OCPP State Controls Hardware, Not Vice Versa
```cpp
// Hardware signals should only UPDATE OCPP inputs
// OCPP state machine should CONTROL chargingEnabled

// Remove direct hardware control of chargingEnabled
// Let OCPP transaction callbacks be the ONLY place that sets chargingEnabled
```

### Fix 4: Only Stop Transaction If Valid
```cpp
// In plug disconnect logic:
if (shouldDisconnect && (gunPhysicallyConnected || batteryConnected))
{
    gunPhysicallyConnected = false;
    batteryConnected = false;
    
    // ONLY stop transaction if one is actually running
    if (transactionLocked && activeTransactionId > 0 && isTransactionRunning(1)) {
        Serial.printf("[PLUG] Stopping transaction %d due to disconnect\n", activeTransactionId);
        endTransaction(nullptr, "EVDisconnected");
    }
}
```

### Fix 5: Prevent State Reset While EV Connected
```cpp
// In state machine poll():
if (currentState == ConnectorState::Finishing && stateAge > FINISHING_TIMEOUT_MS)
{
    // ONLY reset to Available if EV is actually disconnected
    if (!gunPhysicallyConnected && !batteryConnected) {
        Serial.println("[OCPP_SM] ⏱️ Finishing timeout - EV disconnected, transitioning Available");
        forceState(ConnectorState::Available);
    } else {
        Serial.println("[OCPP_SM] ⚠️ Finishing timeout but EV still connected - keeping Finishing state");
    }
}
```

---

## Implementation Plan

1. Add transaction lock mechanism
2. Validate transaction ID before enabling charging
3. Separate hardware signals from control logic
4. Add transaction validity checks before StopTransaction
5. Add EV connection check before state reset

