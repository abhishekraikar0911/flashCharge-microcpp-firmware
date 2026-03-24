# Testing Checklist - CAN Bus Stability Fixes

## 📋 Pre-Testing Setup

### Server-Side Preparation
- [ ] Close stuck transactions (1057, 1058) on server
- [ ] Verify server is running and accessible
- [ ] Check charger is registered in OCPP server
- [ ] Clear any error logs from previous sessions

### Firmware Preparation
- [ ] Build firmware with new fixes (v2.5.0)
- [ ] Flash to ESP32 successfully
- [ ] Verify serial monitor shows boot messages
- [ ] Check firmware version in boot log

### Hardware Preparation
- [ ] ESP32 powered and connected
- [ ] CAN bus wired correctly (CANH, CANL, GND)
- [ ] Charger module powered on
- [ ] Vehicle BMS accessible (if testing with vehicle)

---

## 🧪 Test Suite 1: Firmware Fixes Verification

### Test 1.1: CAN Recovery Tolerance
**Objective**: Verify 5-second timeout prevents false "charger offline" alerts

**Steps**:
1. Monitor serial output during normal operation
2. Observe CAN message timestamps
3. Check for "CHARGER OFFLINE" alerts

**Expected Result**:
- ✅ No false "charger offline" alerts
- ✅ Charger health check passes with 5s tolerance
- ✅ Log shows: `[HEALTH] Status changed: HEALTHY`

**Actual Result**: _______________

---

### Test 1.2: CAN Recovery Flag
**Objective**: Verify voltage-drop disconnect is disabled during CAN recovery

**Steps**:
1. Monitor serial output
2. Wait for CAN BUS-OFF event (or simulate)
3. Check for voltage-drop disconnect messages

**Expected Result**:
- ✅ Log shows: `[CAN] 🚨 BUS-OFF detected, initiating recovery...`
- ✅ Log shows: `[PLUG] ⚠️  CAN recovery active - voltage-drop disconnect disabled`
- ✅ No premature EV disconnect during recovery
- ✅ Log shows: `[CAN] ✅ Bus recovered - normal operation resumed`
- ✅ Log shows: `[PLUG] ✅ CAN recovered - voltage-drop disconnect re-enabled`

**Actual Result**: _______________

---

### Test 1.3: Transaction Completion
**Objective**: Verify transactions complete successfully without getting stuck

**Steps**:
1. Send RemoteStart from OCPP server
2. Wait for charging to start
3. Let charging run for 5+ minutes
4. Send RemoteStop from OCPP server
5. Check server transaction status

**Expected Result**:
- ✅ RemoteStart accepted
- ✅ Charging starts within 5 seconds
- ✅ Charging continues for 5+ minutes without interruption
- ✅ RemoteStop accepted
- ✅ StopTransaction sent successfully
- ✅ Server shows `isActive=false`
- ✅ Transaction ID valid (> 0)

**Actual Result**: _______________

---

## 🔧 Test Suite 2: Hardware Fixes Verification

### Test 2.1: Termination Resistors
**Objective**: Verify 120Ω termination resistors are installed correctly

**Steps**:
1. Power OFF ESP32 and charger module
2. Disconnect CAN bus from ESP32
3. Measure resistance between CANH and CANL with multimeter
4. Reconnect CAN bus

**Expected Result**:
- ✅ Resistance: 55-65Ω (two 120Ω resistors in parallel)
- ✅ If not, install termination resistors

**Actual Result**: _______________

---

### Test 2.2: CAN Wiring Quality
**Objective**: Verify shielded twisted-pair cable is used

**Steps**:
1. Visual inspection of CAN cable
2. Check cable routing (away from power cables)
3. Verify cable length < 3 meters
4. Check shield connection (one end only)

**Expected Result**:
- ✅ Shielded twisted-pair cable (CAT5e or CAN-specific)
- ✅ Cable routed away from 30A power cables (> 10cm separation)
- ✅ Cable length < 3 meters
- ✅ Shield connected to ground at ESP32 end only

**Actual Result**: _______________

---

### Test 2.3: Grounding
**Objective**: Verify proper grounding and no ground loops

**Steps**:
1. Power ON ESP32 and charger module
2. Measure voltage between ESP32 GND and Charger GND with multimeter
3. Check for multiple ground paths

**Expected Result**:
- ✅ Ground voltage < 0.1V
- ✅ Single ground path (star topology)
- ✅ No ground loops

**Actual Result**: _______________

---

### Test 2.4: Ferrite Beads (Optional)
**Objective**: Verify ferrite beads are installed on CAN lines

**Steps**:
1. Visual inspection of CAN lines near ESP32
2. Check for ferrite beads on CANH and CANL

**Expected Result**:
- ✅ Ferrite beads installed on CANH and CANL
- ✅ Positioned within 5cm of ESP32 pins

**Actual Result**: _______________

---

## 🚗 Test Suite 3: Functional Testing

### Test 3.1: CAN Bus Stability (30 minutes)
**Objective**: Verify no CAN BUS-OFF events for extended period

**Steps**:
1. Monitor serial output for 30 minutes
2. Count CAN BUS-OFF events
3. Check for CAN error messages

**Expected Result**:
- ✅ Zero CAN BUS-OFF events in 30 minutes
- ✅ No CAN error messages
- ✅ Continuous CAN message flow
- ✅ Log shows regular: `[CHARGER] RX: 0x00433F01 (Terminal Power)`

**Actual Result**: _______________

---

### Test 3.2: Full Charging Session
**Objective**: Verify complete charging session without interruption

**Steps**:
1. Connect vehicle to charger
2. Send RemoteStart from OCPP server
3. Monitor charging for full session (or 30+ minutes)
4. Send RemoteStop from OCPP server
5. Disconnect vehicle

**Expected Result**:
- ✅ Vehicle detected (gun plugged)
- ✅ RemoteStart accepted
- ✅ Charging starts within 5 seconds
- ✅ Charging continues without interruption
- ✅ MeterValues sent every 60 seconds
- ✅ RemoteStop accepted
- ✅ StopTransaction sent successfully
- ✅ Transaction closed on server
- ✅ Energy metering accurate

**Actual Result**: _______________

---

### Test 3.3: Multiple Charge Cycles
**Objective**: Verify system stability over multiple charge cycles

**Steps**:
1. Perform 3 complete charge cycles
2. Monitor for any degradation or errors
3. Check transaction IDs are sequential

**Expected Result**:
- ✅ All 3 cycles complete successfully
- ✅ No errors or warnings
- ✅ Transaction IDs sequential (no gaps)
- ✅ No memory leaks (check free heap)

**Actual Result**: _______________

---

### Test 3.4: Error Recovery
**Objective**: Verify system recovers from errors gracefully

**Steps**:
1. Simulate CAN BUS-OFF (disconnect CAN wire briefly)
2. Reconnect CAN wire
3. Check system recovery
4. Resume charging

**Expected Result**:
- ✅ System detects CAN BUS-OFF
- ✅ Recovery initiated automatically
- ✅ CAN communication restored
- ✅ Charging can resume after recovery
- ✅ No stuck transactions

**Actual Result**: _______________

---

## 📊 Test Suite 4: Performance Testing

### Test 4.1: Boot Time
**Objective**: Verify boot time is acceptable

**Steps**:
1. Power cycle ESP32
2. Measure time from boot to OCPP connection
3. Check for any delays or errors

**Expected Result**:
- ✅ Boot time < 10 seconds
- ✅ OCPP connection within 5 seconds of WiFi connection
- ✅ No errors during boot

**Actual Result**: _______________

---

### Test 4.2: CAN Latency
**Objective**: Verify CAN message processing is fast

**Steps**:
1. Monitor serial output with timestamps
2. Measure time between CAN TX and RX
3. Check for any delays

**Expected Result**:
- ✅ CAN latency < 10ms
- ✅ No message drops
- ✅ Consistent timing

**Actual Result**: _______________

---

### Test 4.3: Memory Usage
**Objective**: Verify no memory leaks

**Steps**:
1. Check free heap at boot
2. Run system for 1 hour
3. Check free heap again
4. Calculate memory leak rate

**Expected Result**:
- ✅ Free heap at boot: ~180KB
- ✅ Free heap after 1 hour: > 150KB
- ✅ Memory leak rate < 1KB/hour

**Actual Result**: _______________

---

### Test 4.4: CPU Usage
**Objective**: Verify CPU usage is reasonable

**Steps**:
1. Monitor task execution times
2. Check for any task starvation
3. Verify watchdog doesn't trigger

**Expected Result**:
- ✅ CPU usage < 30% average
- ✅ No task starvation
- ✅ No watchdog timeouts

**Actual Result**: _______________

---

## 🔐 Test Suite 5: Safety Testing

### Test 5.1: Emergency Stop
**Objective**: Verify emergency stop works correctly

**Steps**:
1. Start charging
2. Trigger emergency stop (BMS safety flag, overheat, etc.)
3. Check system response

**Expected Result**:
- ✅ Charging stops immediately
- ✅ Hardware stop command sent
- ✅ Transaction ended with "EmergencyStop" reason
- ✅ Fault lock activated (10s stabilization)

**Actual Result**: _______________

---

### Test 5.2: Overvoltage Protection
**Objective**: Verify system stops on overvoltage

**Steps**:
1. Monitor voltage during charging
2. Simulate overvoltage (if possible)
3. Check system response

**Expected Result**:
- ✅ System detects overvoltage (> 85.5V)
- ✅ Charging stops immediately
- ✅ Alert sent to server
- ✅ Transaction ended

**Actual Result**: _______________

---

### Test 5.3: Overcurrent Protection
**Objective**: Verify system stops on overcurrent

**Steps**:
1. Monitor current during charging
2. Simulate overcurrent (if possible)
3. Check system response

**Expected Result**:
- ✅ System detects overcurrent (> 300A)
- ✅ Charging stops immediately
- ✅ Alert sent to server
- ✅ Transaction ended

**Actual Result**: _______________

---

### Test 5.4: Temperature Protection
**Objective**: Verify system stops on overheat

**Steps**:
1. Monitor temperature during charging
2. Simulate overheat (if possible)
3. Check system response

**Expected Result**:
- ✅ System detects overheat (> 80°C)
- ✅ Charging stops immediately
- ✅ Alert sent to server
- ✅ Transaction ended

**Actual Result**: _______________

---

## 📝 Test Summary

### Overall Results

| Test Suite | Tests Passed | Tests Failed | Pass Rate |
|------------|--------------|--------------|-----------|
| Firmware Fixes | ___/3 | ___/3 | ___% |
| Hardware Fixes | ___/4 | ___/4 | ___% |
| Functional Testing | ___/4 | ___/4 | ___% |
| Performance Testing | ___/4 | ___/4 | ___% |
| Safety Testing | ___/4 | ___/4 | ___% |
| **TOTAL** | **___/19** | **___/19** | **___%** |

---

### Critical Issues Found

1. _______________________________________________
2. _______________________________________________
3. _______________________________________________

---

### Recommendations

1. _______________________________________________
2. _______________________________________________
3. _______________________________________________

---

### Sign-Off

**Tested By**: _______________  
**Date**: _______________  
**Firmware Version**: v2.5.0  
**Hardware Revision**: _______________  

**Approved for Production**: ☐ YES  ☐ NO  

**Notes**: 
_______________________________________________
_______________________________________________
_______________________________________________

---

## 📞 Support

For testing issues or questions:
- Review test procedures carefully
- Check serial console logs for errors
- Consult [FIRMWARE_FIXES_IMPLEMENTED.md](FIRMWARE_FIXES_IMPLEMENTED.md)
- Consult [HARDWARE_FIXES_GUIDE.md](HARDWARE_FIXES_GUIDE.md)
- Contact: support@rivotmotors.com
