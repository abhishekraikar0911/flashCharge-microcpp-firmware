/**
 * @file LedService.h
 * @brief Status LED control — blink patterns encode OCPP state across 3 LEDs
 * @layer Service
 *
 * Hardware:
 *   D4  (Orange) — Charger / session state
 *   D15 (Blue)   — Network + OCPP WebSocket connection
 *   D13 (Red)    — Fault / error state
 *
 * State → Pattern mapping:
 *   Available      → Orange STEADY_ON,    Blue SLOW_BLINK,  Red OFF
 *   Preparing      → Orange DOUBLE_PULSE, Blue SLOW_BLINK,  Red OFF
 *   Charging       → Orange SLOW_BLINK,   Blue SLOW_BLINK,  Red OFF
 *   Finishing      → Orange DOUBLE_PULSE, Blue SLOW_BLINK,  Red OFF
 *   SuspendedEVSE  → Orange FAST_BLINK,   Blue SLOW_BLINK,  Red OFF
 *   SuspendedEV    → Orange FAST_BLINK,   Blue SLOW_BLINK,  Red OFF
 *   Faulted        → Orange OFF,          Blue any,         Red FAST_BLINK
 *   Offline/no WS  → Orange any,          Blue STEADY_ON,   Red OFF
 *   Booting        → Orange FAST_BLINK,   Blue FAST_BLINK,  Red OFF
 *
 * Uses: g_app.gpio, g_app.timer, getChargePointStatus(), g_networkManager, ocpp::isConnected()
 * Does NOT access pins directly — uses g_app.gpio abstraction.
 */
#pragma once

#include <cstdint>

namespace prod {

/**
 * @brief LED blink pattern types.
 *
 * Each pattern is computed purely from elapsed time — no shared toggle state.
 * Patterns reset their phase when they change, keeping blinks crisp.
 */
enum class LedPattern : uint8_t {
    OFF,           ///< Always OFF
    STEADY_ON,     ///< Always ON
    SLOW_BLINK,    ///< 500 ms ON / 500 ms OFF  (1 000 ms cycle)
    FAST_BLINK,    ///< 250 ms ON / 250 ms OFF  (  500 ms cycle)
    DOUBLE_PULSE,  ///< ON 150ms, OFF 150ms, ON 150ms, OFF 550ms (1 000 ms cycle)
                   ///< Used for transient states: Preparing, Finishing
};

class LedService {
public:
    static LedService& instance() {
        static LedService inst;
        return inst;
    }

    void begin();
    void poll();

private:
    LedService() = default;

    /**
     * @brief Compute the pin level for a given pattern at the current time.
     *
     * @param pattern  The desired blink pattern.
     * @param ledIdx   LED slot index (0=Orange, 1=Blue, 2=Red).
     *                 Each slot has an independent phase start so LEDs don't
     *                 share a blink timer and can transition independently.
     * @param now      Current millisecond timestamp.
     * @return true = pin HIGH, false = pin LOW.
     */
    bool computePattern(LedPattern pattern, uint8_t ledIdx, unsigned long now);

    static constexpr uint8_t NUM_LEDS = 3;

    // Per-LED state for phase tracking (reset when pattern changes)
    LedPattern    _lastPattern[NUM_LEDS] = { LedPattern::OFF,
                                             LedPattern::OFF,
                                             LedPattern::OFF };
    unsigned long _phaseStart[NUM_LEDS]  = { 0, 0, 0 };
};

} // namespace prod
