/**
 * @file CrashForensics.cpp
 * @brief Implementation of crash forensics — NVS-backed pre-crash state tracking.
 *
 * NVS namespace: "cf" (crash forensics) — separate from "ocpp_prod".
 *
 * NVS keys:
 *   "activity"  : string  — last activity label
 *   "tls_ms"    : uint32  — last TLS connect duration (ms)
 *   "tls_max"   : uint32  — max TLS duration ever seen (ms)
 *   "min_heap"  : uint32  — lowest free heap (bytes)
 *   "min_blk"   : uint32  — lowest largest-free-block (bytes)
 */

#include "system/CrashForensics.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>   // heap_caps_get_largest_free_block()

namespace prod {

// ── RAM state ────────────────────────────────────────────────────────────
static char     s_activity[32]    = "IDLE";
static uint32_t s_uptimeAtPersist = 0;
static uint32_t s_tlsDurationMs   = 0;
static uint32_t s_maxTlsDurationMs = 0;
static uint32_t s_minHeapBytes    = UINT32_MAX;
static uint32_t s_minLargestBlock = UINT32_MAX;

static const char* CF_NS = "cf";

// ── Setters ───────────────────────────────────────────────────────────────

void CrashForensics::setActivity(const char* activity) {
    strncpy(s_activity, activity, sizeof(s_activity) - 1);
    s_activity[sizeof(s_activity) - 1] = '\0';
}

void CrashForensics::setTlsDurationMs(uint32_t ms) {
    s_tlsDurationMs = ms;
    if (ms > s_maxTlsDurationMs) {
        s_maxTlsDurationMs = ms;
    }
}

void CrashForensics::updateHeap() {
    uint32_t freeHeap    = (uint32_t)ESP.getFreeHeap();
    uint32_t largestBlk  = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    if (freeHeap   < s_minHeapBytes)    s_minHeapBytes    = freeHeap;
    if (largestBlk < s_minLargestBlock) s_minLargestBlock = largestBlk;
}

// ── NVS persistence ───────────────────────────────────────────────────────

void CrashForensics::persist() {
    updateHeap();  // always snapshot heap at persist time
    s_uptimeAtPersist = millis();

    Preferences prefs;
    if (!prefs.begin(CF_NS, false)) return;
    prefs.putString("activity",  s_activity);
    prefs.putULong ("uptime_ms", s_uptimeAtPersist);
    prefs.putULong ("tls_ms",    s_tlsDurationMs);
    prefs.putULong ("tls_max",   s_maxTlsDurationMs);
    prefs.putULong ("min_heap",  s_minHeapBytes    == UINT32_MAX ? 0 : s_minHeapBytes);
    prefs.putULong ("min_blk",   s_minLargestBlock == UINT32_MAX ? 0 : s_minLargestBlock);
    prefs.end();
}

void CrashForensics::load() {
    Preferences prefs;
    if (!prefs.begin(CF_NS, true)) return;  // read-only
    String act = prefs.getString("activity", "UNKNOWN");
    strncpy(s_activity, act.c_str(), sizeof(s_activity) - 1);
    s_activity[sizeof(s_activity) - 1] = '\0';
    s_uptimeAtPersist  = prefs.getULong("uptime_ms", 0);
    s_tlsDurationMs    = prefs.getULong("tls_ms",   0);
    s_maxTlsDurationMs = prefs.getULong("tls_max",  0);
    s_minHeapBytes     = prefs.getULong("min_heap", 0);
    s_minLargestBlock  = prefs.getULong("min_blk",  0);
    prefs.end();

    Serial.printf(
        "[FORENSICS] \xf0\x9f\x94\x8d Loaded: activity=%s uptime=%lu tlsMs=%lu tlsMax=%lu"
        " minHeap=%lu minBlk=%lu\n",
        s_activity,
        (unsigned long)s_uptimeAtPersist,
        (unsigned long)s_tlsDurationMs,
        (unsigned long)s_maxTlsDurationMs,
        (unsigned long)s_minHeapBytes,
        (unsigned long)s_minLargestBlock);
}

void CrashForensics::clear() {
    strncpy(s_activity, "IDLE", sizeof(s_activity) - 1);
    s_uptimeAtPersist  = 0;
    s_tlsDurationMs    = 0;
    // NOTE: intentionally keep s_maxTlsDurationMs across boots — it's a
    // fleet metric showing worst-case TLS on this specific unit/site.
    s_minHeapBytes    = UINT32_MAX;
    s_minLargestBlock = UINT32_MAX;

    Preferences prefs;
    if (!prefs.begin(CF_NS, false)) return;
    prefs.putString("activity", "IDLE");
    prefs.putULong ("uptime_ms", 0);
    prefs.putULong ("tls_ms",   0);
    // tls_max is NOT cleared — preserved across reboots intentionally
    prefs.putULong ("min_heap", 0);
    prefs.putULong ("min_blk",  0);
    prefs.end();
}

// ── Accessors ─────────────────────────────────────────────────────────────

const char* CrashForensics::getActivity()         { return s_activity; }
uint32_t    CrashForensics::getUptimeAtPersist()  { return s_uptimeAtPersist; }
uint32_t    CrashForensics::getTlsDurationMs()    { return s_tlsDurationMs; }
uint32_t    CrashForensics::getMaxTlsDurationMs() { return s_maxTlsDurationMs; }

uint32_t    CrashForensics::getMinHeapBytes() {
    return (s_minHeapBytes == UINT32_MAX) ? 0 : s_minHeapBytes;
}
uint32_t    CrashForensics::getMinLargestBlock() {
    return (s_minLargestBlock == UINT32_MAX) ? 0 : s_minLargestBlock;
}
uint32_t    CrashForensics::getCurrentHeapBytes() {
    return (uint32_t)ESP.getFreeHeap();
}
uint32_t    CrashForensics::getCurrentLargestBlock() {
    return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

} // namespace prod
