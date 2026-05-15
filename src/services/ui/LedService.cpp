/**
 * @file LedService.cpp
 * @brief Blink-pattern LED driver — encodes all OCPP charger states across 3 LEDs.
 *
 * Priority order (highest to lowest):
 *   1. Fault      → Red FAST_BLINK, Orange OFF
 *   2. Offline    → Blue STEADY_ON (no network)
 *   3. Connecting → Blue FAST_BLINK (network up, WebSocket not yet established)
 *   4. OCPP state → Orange pattern per state table in LedService.h
 */
#include "services/ui/LedService.h"
#include "app/AppContext.h"
#include "config/hardware.h"
#include "services/network/NetworkManager.h"
#include "services/ocpp/OcppClient.h"
#include "system/state/SystemState.h"
#include <MicroOcpp.h>

namespace prod {

// ─────────────────────────────────────────────────────────────────────────────
// begin()
// ─────────────────────────────────────────────────────────────────────────────
void LedService::begin() {
    if (g_app.gpio) {
        g_app.gpio->setMode(LED_CHARGER_STATUS, IGpio::GPIO_OUTPUT);
        g_app.gpio->setMode(LED_NETWORK_STATUS, IGpio::GPIO_OUTPUT);
        g_app.gpio->setMode(LED_FAULT_STATUS,   IGpio::GPIO_OUTPUT);
        // All LEDs start OFF — patterns take over on first poll()
        g_app.gpio->write(LED_CHARGER_STATUS, false);
        g_app.gpio->write(LED_NETWORK_STATUS, false);
        g_app.gpio->write(LED_FAULT_STATUS,   false);
    }
    if (g_app.logger) g_app.logger->log(ILogger::Level::INFO, "LED_SVC", "Started (pattern mode)");
}

// ─────────────────────────────────────────────────────────────────────────────
// computePattern()
//
// Returns the logical pin level (true=HIGH, false=LOW) for a given pattern,
// computed purely from the elapsed time since this pattern was last set.
// Each LED has its own phase-start timestamp so blinks are fully independent.
// ─────────────────────────────────────────────────────────────────────────────
bool LedService::computePattern(LedPattern pattern, uint8_t ledIdx, unsigned long now) {
    // Reset phase start whenever the pattern changes — keeps the transition crisp.
    if (pattern != _lastPattern[ledIdx]) {
        _phaseStart[ledIdx]  = now;
        _lastPattern[ledIdx] = pattern;
    }

    unsigned long elapsed = now - _phaseStart[ledIdx];

    switch (pattern) {
        case LedPattern::OFF:
            return false;

        case LedPattern::STEADY_ON:
            return true;

        case LedPattern::SLOW_BLINK:
            // 500 ms ON, 500 ms OFF  →  1 000 ms period
            return (elapsed % 1000UL) < 500UL;

        case LedPattern::FAST_BLINK:
            // 250 ms ON, 250 ms OFF  →  500 ms period
            return (elapsed % 500UL) < 250UL;

        case LedPattern::DOUBLE_PULSE: {
            // ON 150 ms | OFF 150 ms | ON 150 ms | OFF 550 ms  →  1 000 ms period
            // Encodes "transition" states (Preparing / Finishing): two quick flashes,
            // then a long pause — visually distinct from both STEADY_ON and SLOW_BLINK.
            uint32_t t = elapsed % 1000UL;
            return (t < 150UL) || (t >= 300UL && t < 450UL);
        }

        default:
            return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// poll()
// Called periodically (every ~50 ms) from the UI task.
// ─────────────────────────────────────────────────────────────────────────────
void LedService::poll() {
    if (!g_app.gpio) return;

    unsigned long now = g_app.timer ? g_app.timer->millis() : 0;

    // ── Read system state ────────────────────────────────────────────────────
    bool ocppInit  = SystemState::instance().getOcppInitialized();
    bool faultLock = SystemState::instance().getFaultLockActive();
    bool networkOk = prod::g_networkManager.isConnected();
    bool wsOk      = false;

    ChargePointStatus libStatus = ChargePointStatus_Available;
    if (ocppInit) {
        libStatus = getChargePointStatus(1);
        wsOk      = ocpp::isConnected();

        // Treat OCPP Faulted state the same as a hardware fault lock
        if (libStatus == ChargePointStatus_Faulted) {
            faultLock = true;
        }
    }

    // ── Determine patterns ───────────────────────────────────────────────────

    // ── ORANGE (D4): Charger / session state ─────────────────────────────
    // Priority: fault clears Orange, booting uses FAST_BLINK,
    // then the OCPP connector state drives the pattern.
    LedPattern orangePattern;
    if (faultLock) {
        // Fault active — turn Orange OFF so Red is the only signal
        orangePattern = LedPattern::OFF;
    } else if (!ocppInit) {
        // Booting / waiting for NTP + OCPP init
        orangePattern = LedPattern::FAST_BLINK;
    } else {
        switch (libStatus) {
            case ChargePointStatus_Available:
                // Ready and idle
                orangePattern = LedPattern::STEADY_ON;
                break;

            case ChargePointStatus_Preparing:
            case ChargePointStatus_Finishing:
                // Transient: gun connecting or session winding down.
                // DOUBLE_PULSE = two quick flashes → "pay attention" state.
                orangePattern = LedPattern::DOUBLE_PULSE;
                break;

            case ChargePointStatus_Charging:
                // Active power delivery — calm, rhythmic blink
                orangePattern = LedPattern::SLOW_BLINK;
                break;

            case ChargePointStatus_SuspendedEVSE:
            case ChargePointStatus_SuspendedEV:
                // Charging paused (vehicle or EVSE side) — fast blink = warning
                orangePattern = LedPattern::FAST_BLINK;
                break;

            case ChargePointStatus_Faulted:
                // Already handled by faultLock path above, but guard anyway
                orangePattern = LedPattern::OFF;
                break;

            case ChargePointStatus_Unavailable:
            default:
                // Reserved / unavailable — steady to indicate "present but blocked"
                orangePattern = LedPattern::STEADY_ON;
                break;
        }
    }

    // ── BLUE (D15): Network + OCPP WebSocket connection ───────────────────
    // STEADY_ON  = no network (alarm: offline)
    // FAST_BLINK = network up, establishing WebSocket / waiting for OCPP
    // SLOW_BLINK = fully connected to CSMS
    LedPattern bluePattern;
    if (!networkOk) {
        bluePattern = LedPattern::STEADY_ON;   // No network — steady = alarm
    } else if (!ocppInit || !wsOk) {
        bluePattern = LedPattern::FAST_BLINK;  // Network up, OCPP not ready
    } else {
        bluePattern = LedPattern::SLOW_BLINK;  // Fully connected to CSMS
    }

    // ── RED (D13): Fault indicator ────────────────────────────────────────
    LedPattern redPattern = faultLock ? LedPattern::FAST_BLINK : LedPattern::OFF;

    // ── Apply patterns to GPIO ───────────────────────────────────────────────
    g_app.gpio->write(LED_CHARGER_STATUS, computePattern(orangePattern, 0, now));
    g_app.gpio->write(LED_NETWORK_STATUS, computePattern(bluePattern,   1, now));
    g_app.gpio->write(LED_FAULT_STATUS,   computePattern(redPattern,    2, now));

    // ── Debug log every 2 s ──────────────────────────────────────────────────
    static unsigned long lastDebugLog = 0;
    if (now - lastDebugLog >= 2000UL) {
        lastDebugLog = now;
        if (g_app.logger) {
            g_app.logger->logf(ILogger::Level::DEBUG, "LED_SVC",
                "ocppState=%d fault=%d net=%d ws=%d | O=%d B=%d R=%d",
                (int)libStatus, (int)faultLock,
                (int)networkOk, (int)wsOk,
                (int)orangePattern, (int)bluePattern, (int)redPattern);
        }
    }
}

} // namespace prod
