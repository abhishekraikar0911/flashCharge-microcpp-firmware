# Hardware Fixes Guide - CAN Bus Stability

## 🔧 Priority 1: CAN Bus Hardware (CRITICAL)

### Problem
- CAN BUS-OFF events every 2-5 minutes
- EMI from 30A charging current
- Missing termination resistors
- Poor cable quality

---

## 📋 Required Components

| Component | Specification | Quantity | Purpose |
|-----------|--------------|----------|---------|
| Termination Resistor | 120Ω, 1/4W | 2 | CAN bus termination |
| Shielded Cable | CAT5e or CAN-specific | 3m | Reduce EMI |
| Ferrite Beads | 10-100MHz suppression | 2 | Filter high-frequency noise |
| Heat Shrink Tubing | 3mm diameter | 10cm | Insulate connections |

---

## 🔌 Fix #1: Add Termination Resistors

### Why?
CAN bus requires 120Ω termination at BOTH ends for proper signal integrity.

### Installation:

#### Location 1: ESP32 End
```
CANH ----[120Ω]---- CANL
```

**Steps**:
1. Solder 120Ω resistor between CANH and CANL pins
2. Use heat shrink tubing to insulate
3. Verify resistance: ~60Ω between CANH and CANL (both terminators in parallel)

#### Location 2: Charger Module End
```
CANH ----[120Ω]---- CANL
```

**Steps**:
1. Locate CAN connector on charger module
2. Solder 120Ω resistor between CANH and CANL
3. Insulate with heat shrink tubing

### Verification:
```bash
# Measure resistance between CANH and CANL with multimeter
# Expected: ~60Ω (two 120Ω resistors in parallel)
```

---

## 🔗 Fix #2: Replace CAN Wiring

### Why?
- Unshielded wires pick up EMI from 30A charging current
- Long wire runs increase susceptibility to noise

### Cable Options:

#### Option A: CAT5e Ethernet Cable (Recommended)
- **Pros**: Readily available, twisted-pair, good shielding
- **Cons**: Overkill for short runs
- **Wiring**:
  - Orange pair: CANH
  - Orange/White pair: CANL
  - Green pair: GND
  - Shield: Connect to chassis ground at ONE end only

#### Option B: CAN-Specific Cable
- **Pros**: Purpose-built, excellent noise immunity
- **Cons**: More expensive
- **Spec**: 2-wire twisted-pair, 120Ω characteristic impedance

### Installation:
1. **Disconnect power** from ESP32 and charger module
2. **Remove old wires** (unshielded)
3. **Route new cable** away from power cables (30A charging wires)
4. **Connect wires**:
   - CANH (ESP32 GPIO21) → CANH (Charger)
   - CANL (ESP32 GPIO22) → CANL (Charger)
   - GND (ESP32) → GND (Charger) - **CRITICAL**
5. **Connect shield** to chassis ground at ESP32 end ONLY (avoid ground loops)

### Cable Routing Rules:
- ✅ Keep CAN cable < 3 meters
- ✅ Route away from power cables (minimum 10cm separation)
- ✅ Avoid sharp bends (minimum 5cm radius)
- ✅ Use cable ties every 20cm
- ❌ Do NOT run parallel to 30A charging wires
- ❌ Do NOT coil excess cable (creates antenna)

---

## 🌍 Fix #3: Improve Grounding

### Why?
Ground loops and poor grounding cause voltage differences that corrupt CAN signals.

### Star Grounding Topology:
```
        [Common Ground Point]
               |
       +-------+-------+
       |               |
   [ESP32 GND]    [Charger GND]
```

### Implementation:
1. **Identify common ground point** (chassis, power supply negative terminal)
2. **Connect ESP32 GND** to common ground with thick wire (18 AWG)
3. **Connect Charger GND** to common ground with thick wire (18 AWG)
4. **Verify ground voltage** < 0.1V between ESP32 and charger module

### Ground Loop Prevention:
- ✅ Single ground connection per device
- ✅ Shield connected at ONE end only
- ❌ Do NOT create multiple ground paths
- ❌ Do NOT connect shield at both ends

---

## 🧲 Fix #4: Add Ferrite Beads

### Why?
Ferrite beads filter high-frequency noise (>10MHz) that causes CAN errors.

### Installation:

#### Location 1: ESP32 CAN Lines
```
ESP32 GPIO21 (TX) ---[Ferrite]--- CANH
ESP32 GPIO22 (RX) ---[Ferrite]--- CANL
```

**Steps**:
1. Slide ferrite bead over CANH wire near ESP32
2. Slide ferrite bead over CANL wire near ESP32
3. Position beads within 5cm of ESP32 pins

#### Location 2: Charger Module CAN Lines (Optional)
- Same as above, but near charger module connector

### Ferrite Bead Selection:
- **Impedance**: 100-300Ω @ 100MHz
- **Type**: Clamp-on or through-hole
- **Size**: Suitable for wire gauge (typically 22-24 AWG)

---

## 📊 Verification & Testing

### Step 1: Visual Inspection
- [ ] Termination resistors installed at both ends
- [ ] Shielded cable used for CAN bus
- [ ] Cable routed away from power wires
- [ ] Ferrite beads installed near ESP32
- [ ] Ground connections secure

### Step 2: Electrical Testing
```bash
# Power OFF - Measure resistance
Multimeter between CANH and CANL: ~60Ω (both terminators)

# Power OFF - Measure ground continuity
Multimeter between ESP32 GND and Charger GND: < 1Ω

# Power OFF - Check ground voltage
Multimeter between ESP32 GND and Charger GND: < 0.1V
```

### Step 3: Oscilloscope Testing (Optional)
```
Probe CANH and CANL during operation:
- Clean square waves (no ringing)
- Voltage swing: 2-3V differential
- No noise spikes > 0.5V
```

### Step 4: Functional Testing
```bash
# Flash firmware and monitor serial output
pio run -e charger_esp32_production --target upload
pio device monitor --baud 115200

# Expected output:
[CAN] ✅ Bus initialized successfully
[CAN] ✅ No BUS-OFF events for 10+ minutes
[CHARGER] RX: 0x00433F01 (Terminal Power)
[CHARGER]   ← Terminal: V=76.2V I=1.8A P=137.2W
```

---

## 🎯 Success Criteria

### Before Hardware Fixes:
- ❌ CAN BUS-OFF every 2-5 minutes
- ❌ Stuck transactions
- ❌ Premature EV disconnect

### After Hardware Fixes:
- ✅ No CAN BUS-OFF events for 30+ minutes
- ✅ Transactions complete successfully
- ✅ Stable charging for full session
- ✅ Clean CAN signals on oscilloscope

---

## ⚠️ Safety Warnings

1. **Disconnect power** before working on hardware
2. **Verify polarity** before connecting wires
3. **Insulate all connections** with heat shrink tubing
4. **Test with multimeter** before powering on
5. **Monitor temperature** during first test (charger module should stay < 60°C)

---

## 🔍 Troubleshooting

### Issue: Still getting BUS-OFF events
**Possible Causes**:
- Termination resistors not installed correctly
- Ground loop present
- Cable too long (> 3m)
- EMI from nearby equipment

**Solution**:
1. Verify 60Ω resistance between CANH and CANL
2. Check for multiple ground paths
3. Shorten cable length
4. Move away from EMI sources (motors, relays, switching power supplies)

### Issue: Intermittent communication
**Possible Causes**:
- Loose connections
- Poor solder joints
- Cable damage

**Solution**:
1. Check all connections with multimeter
2. Re-solder suspect joints
3. Replace damaged cable sections

### Issue: Ground voltage > 0.1V
**Possible Causes**:
- Ground loop
- High current through ground wire
- Poor ground connection

**Solution**:
1. Remove extra ground paths (keep only one)
2. Use thicker ground wire (18 AWG or larger)
3. Clean ground connection points (remove oxidation)

---

## 📅 Implementation Timeline

| Task | Duration | Priority |
|------|----------|----------|
| Add termination resistors | 30 min | HIGH |
| Replace CAN wiring | 1 hour | HIGH |
| Improve grounding | 30 min | MEDIUM |
| Add ferrite beads | 15 min | LOW |
| Testing & verification | 1 hour | HIGH |

**Total Time**: ~3 hours

---

## 📞 Support

For hardware questions:
- Check wiring diagram in `docs/hardware/`
- Review CAN bus specification (ISO 11898)
- Contact: support@rivotmotors.com

---

## 📚 References

- [CAN Bus Specification (ISO 11898)](https://www.iso.org/standard/63648.html)
- [ESP32 TWAI Driver Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/twai.html)
- [CAN Bus Termination Guide](https://www.ti.com/lit/an/sloa101b/sloa101b.pdf)
