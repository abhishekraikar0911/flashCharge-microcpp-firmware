#pragma once

/**
 * @file CrashForensics.h
 * @brief Crash forensics: tracks last activity, TLS duration, and heap stats.
 *
 * All fields are persisted to NVS before critical operations so that on
 * the NEXT boot, the WDT_CRASH report includes exactly what the firmware
 * was doing when it died — enabling remote diagnosis without serial access.
 *
 * NVS namespace: "cf"
 *   activity  → last activity label before crash
 *   uptime_ms → uptime when the activity was logged
 *   tls_ms    → last TLS connect duration (ms)
 *   tls_max   → worst-case TLS duration ever seen (ms) — fleet diagnostic
 *   min_heap  → lowest free heap seen (bytes)
 *   min_blk   → lowest largest-free-block seen (bytes) — heap fragmentation
 */

#include <stdint.h>

namespace prod {

class CrashForensics {
public:

    // ── Activity labels ──────────────────────────────────────────────────
    // Set BEFORE the operation. Persisted to NVS immediately so that if
    // the operation crashes, the next boot knows what was happening.

    // Boot / modem sequence
    static constexpr const char* ACT_IDLE             = "IDLE";
    static constexpr const char* ACT_BOOT             = "BOOT";
    static constexpr const char* ACT_MODEM_INIT       = "MODEM_INIT";
    static constexpr const char* ACT_NETOPEN          = "NETOPEN";
    static constexpr const char* ACT_NTP_SYNC         = "NTP_SYNC";
    static constexpr const char* ACT_OCPP_INIT        = "OCPP_INIT";

    // Network / TLS
    static constexpr const char* ACT_TLS_CONNECT      = "TLS_CONNECT";
    static constexpr const char* ACT_GSM_HANDSHAKE    = "GSM_HANDSHAKE";

    // OCPP operations
    static constexpr const char* ACT_AUTHORIZE        = "AUTHORIZE";
    static constexpr const char* ACT_START_TX         = "START_TX";
    static constexpr const char* ACT_STOP_TX          = "STOP_TX";
    static constexpr const char* ACT_REMOTE_START     = "REMOTE_START";
    static constexpr const char* ACT_REMOTE_STOP      = "REMOTE_STOP";
    static constexpr const char* ACT_SEND_VEHICLE_INFO = "SEND_VEHICLE_INFO";
    static constexpr const char* ACT_SEND_FAULT       = "SEND_FAULT";
    static constexpr const char* ACT_OCPP_POLL        = "OCPP_POLL";
    static constexpr const char* ACT_OTA_DOWNLOAD     = "OTA_DOWNLOAD";

    // Hardware
    static constexpr const char* ACT_CAN_RX           = "CAN_RX";
    static constexpr const char* ACT_CAN_TX           = "CAN_TX";

    // ── Setters (in-RAM, fast — no NVS write) ────────────────────────────
    static void setActivity(const char* activity);

    // Updates both last-TLS-duration AND max-TLS-duration-ever-seen.
    // maxTlsDuration across a fleet shows which sites have slow networks.
    static void setTlsDurationMs(uint32_t ms);

    // Samples ESP.getFreeHeap() + heap_caps_get_largest_free_block().
    // Tracks min values to detect both heap exhaustion and fragmentation.
    // Call this before any large allocation or serialisation operation.
    //
    // Why largestFreeBlock matters:
    //   Free Heap = 80 KB, Largest Block = 3 KB → heap is fragmented.
    //   malloc(10 KB) will FAIL even though 80 KB appears free.
    static void updateHeap();

    // ── NVS persistence ──────────────────────────────────────────────────
    // persist() — call immediately BEFORE a risky/blocking operation.
    //             Writes current RAM state to flash so a crash preserves context.
    // load()    — call ONCE at boot before building the WDT_CRASH report.
    // clear()   — call after intentional resets to avoid stale data.
    static void persist();
    static void load();
    static void clear();

    // ── Accessors (for crash report and DataTransfer payload) ─────────────
    static const char* getActivity();
    static uint32_t    getUptimeAtPersist();   // uptime when persist() was called
    static uint32_t    getTlsDurationMs();     // last TLS connect time
    static uint32_t    getMaxTlsDurationMs();  // worst-case TLS ever seen
    static uint32_t    getMinHeapBytes();      // lowest free heap seen
    static uint32_t    getMinLargestBlock();   // lowest largest-free-block (fragmentation)
    static uint32_t    getCurrentHeapBytes();
    static uint32_t    getCurrentLargestBlock();
};

} // namespace prod
