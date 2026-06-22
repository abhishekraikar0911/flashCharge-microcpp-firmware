#include "services/ota/OtaManager.h"
#include "config/production_config.h"
#include "system/security/SecurityManager.h"
#include "services/safety/HealthMonitor.h"
#include "services/ocpp/OcppClient.h"
#include "config/version.h"
#include "system/CrashForensics.h"
#include <Update.h>
#include <MicroOcpp/Core/Context.h>
#include <MicroOcpp/Core/Ftp.h>
#include <MicroOcpp/Model/Model.h>
#include <MicroOcpp/Model/FirmwareManagement/FirmwareService.h>
#include <mbedtls/sha256.h>

namespace
{
    static const size_t OTA_SIGNATURE_SIZE = 64;
    static const size_t OTA_WRITE_BUFFER = 256;

    static bool otaActive = false;
    static bool shaInit = false;
    static mbedtls_sha256_context shaCtx;
    static uint8_t sigRing[OTA_SIGNATURE_SIZE];
    static size_t sigHead = 0;
    static size_t sigCount = 0;
    static uint32_t totalReceived = 0;

    static uint8_t writeBuf[OTA_WRITE_BUFFER];
    static size_t writeLen = 0;
    static bool _updateValid = false;

    static void otaReset()
    {
        if (shaInit)
        {
            mbedtls_sha256_free(&shaCtx);
            shaInit = false;
        }
        otaActive = false;
        sigHead = 0;
        sigCount = 0;
        totalReceived = 0;
        writeLen = 0;
        // _updateValid is NOT reset here because MicroOcpp needs to read it
        // after otaReset() is called.
    }

    static bool otaStart()
    {
        otaReset();
        _updateValid = false;
        mbedtls_sha256_init(&shaCtx);
        if (mbedtls_sha256_starts_ret(&shaCtx, 0) != 0)
        {
            Serial.println("[OTA] ? SHA256 start failed");
            return false;
        }
        shaInit = true;
        otaActive = true;
        return true;
    }

    static bool otaFlushWriteBuf()
    {
        if (writeLen == 0)
        {
            return true;
        }

        // IMPROVEMENT: Batch SHA256 update over entire buffer — one mbedtls
        // call per flush instead of one call per byte (~100x faster)
        if (mbedtls_sha256_update_ret(&shaCtx, writeBuf, writeLen) != 0)
        {
            Serial.println("[OTA] ❌ SHA256 batch update failed");
            return false;
        }

        size_t written = Update.write(writeBuf, writeLen);
        if (written != writeLen)
        {
            Serial.printf("[OTA] ❌ Write failed: %u/%u bytes\n", written, (unsigned)writeLen);
            return false;
        }

        writeLen = 0;
        return true;
    }

    static bool otaWriteOldest(uint8_t b)
    {
        // Accumulate evicted byte into write buffer.
        // SHA256 is now updated in otaFlushWriteBuf() as a batch.
        writeBuf[writeLen++] = b;
        if (writeLen >= OTA_WRITE_BUFFER)
        {
            return otaFlushWriteBuf();
        }
        return true;
    }
}

namespace prod
{
    void OTAManager::init()
    {
        Serial.println("[OTA] ?? OTA Manager initialized");

        // Check if previous update was successful
        if (checkUpdateSuccess())
        {
            Serial.println("[OTA] ? Previous firmware update successful");
        }
    }

    size_t OTAManager::onFirmwareData(const unsigned char *buf, size_t size)
    {
        if (!Update.isRunning())
        {
            // H5 FIX: Anti-rollback — reject firmware older than current version
            // The ESP32 app descriptor contains version at bytes 48-59 (app_desc_t.version)
            // We parse major.minor.patch from the incoming binary's fixed offset.
            if (size >= 64)
            {
                // app_desc_t.version is a 32-byte null-terminated string starting at offset 48
                const char* incomingVer = reinterpret_cast<const char*>(buf + 48);
                int inMajor = 0, inMinor = 0, inPatch = 0;
                if (sscanf(incomingVer, "%d.%d.%d", &inMajor, &inMinor, &inPatch) == 3)
                {
                    bool isDowngrade =
                        (inMajor < FIRMWARE_VERSION_MAJOR) ||
                        (inMajor == FIRMWARE_VERSION_MAJOR && inMinor < FIRMWARE_VERSION_MINOR) ||
                        (inMajor == FIRMWARE_VERSION_MAJOR && inMinor == FIRMWARE_VERSION_MINOR && inPatch < FIRMWARE_VERSION_PATCH);

                    if (isDowngrade)
                    {
                        Serial.printf("[OTA] \xe2\x9b\x94 ROLLBACK REJECTED: incoming v%d.%d.%d < current v%s\n",
                                      inMajor, inMinor, inPatch, FIRMWARE_VERSION);
                        ocpp::sendSystemAlert("OTA_ROLLBACK_BLOCKED",
                                              "Firmware downgrade attempt blocked by anti-rollback policy", "Critical");
                        g_persistence.recordLastError("OTA_ROLLBACK_BLOCKED");
                        return 0;
                    }
                    Serial.printf("[OTA] \xe2\x9c\x85 Version check passed: incoming v%d.%d.%d >= current v%s\n",
                                  inMajor, inMinor, inPatch, FIRMWARE_VERSION);
                }
                else
                {
                    Serial.println("[OTA] \xe2\x9a\xa0\xef\xb8\x8f Version string not found at expected offset — proceeding without version check");
                }
            }

            Serial.printf("[OTA] Starting update (size: %u bytes)\n", UPDATE_SIZE_UNKNOWN);

            if (!Update.begin(UPDATE_SIZE_UNKNOWN))
            {
                Serial.printf("[OTA] ? Update.begin failed: %s\n", Update.errorString());
                return 0;
            }

            if (!otaStart())
            {
                Update.abort();
                return 0;
            }

            // Mark forensics so if we crash during download, we know exactly where
            CrashForensics::setActivity(CrashForensics::ACT_OTA_DOWNLOAD);
            CrashForensics::persist();
        }

        if (!otaActive || !shaInit)
        {
            Serial.println("[OTA] ? OTA state not initialized");
            return 0;
        }

        for (size_t i = 0; i < size; i++)
        {
            uint8_t b = buf[i];
            totalReceived++;

            if (sigCount < OTA_SIGNATURE_SIZE)
            {
                sigRing[(sigHead + sigCount) % OTA_SIGNATURE_SIZE] = b;
                sigCount++;
                continue;
            }

            uint8_t oldest = sigRing[sigHead];
            sigRing[sigHead] = b;
            sigHead = (sigHead + 1) % OTA_SIGNATURE_SIZE;

            if (!otaWriteOldest(oldest))
            {
                Update.abort();
                otaReset();
                return 0;
            }
        }

        if (!otaFlushWriteBuf())
        {
            Update.abort();
            otaReset();
            return 0;
        }

        Serial.printf("[OTA] 📦 Progress: %u bytes\n", Update.progress());
        return size;
    }

    void OTAManager::onDownloadComplete(int reason)
    {
        // FIX: MO_FtpCloseReason_Success = 1 (NOT 0).
        // Previous check `reason != 0` incorrectly aborted on success.
        if (reason != (int)MO_FtpCloseReason_Success)
        {
            Serial.printf("[OTA] ❌ Download failed (reason: %d)\n", reason);
            Update.abort();
            otaReset();
            return;
        }

        if (!otaActive || sigCount < OTA_SIGNATURE_SIZE || totalReceived <= OTA_SIGNATURE_SIZE)
        {
            Serial.println("[OTA] ? Invalid firmware payload (missing signature)");
            Update.abort();
            g_persistence.recordLastError("OTA_INVALID_PAYLOAD");
            otaReset();
            return;
        }

        if (!otaFlushWriteBuf())
        {
            Update.abort();
            otaReset();
            return;
        }

        uint8_t hash[32] = {0};
        if (mbedtls_sha256_finish_ret(&shaCtx, hash) != 0)
        {
            Serial.println("[OTA] ? SHA256 finalize failed");
            Update.abort();
            otaReset();
            return;
        }

        uint8_t sig[OTA_SIGNATURE_SIZE];
        for (size_t i = 0; i < OTA_SIGNATURE_SIZE; i++)
        {
            sig[i] = sigRing[(sigHead + i) % OTA_SIGNATURE_SIZE];
        }

        // IMPROVEMENT: Feed watchdog before ECDSA verify — it can take
        // significant time and risk a WDT reset mid-OTA.
        g_healthMonitor.feed();
        if (!g_securityManager.verifyOTASignature(hash, sizeof(hash), sig, sizeof(sig)))
        {
            Serial.println("[OTA] ❌ Signature verification failed");
            Update.abort();
            g_persistence.recordLastError("OTA_SIG_INVALID");
            ocpp::sendSystemAlert("OTA_SIGNATURE_INVALID", "Firmware signature check failed", "Critical");
            otaReset();
            return;
        }

        if (Update.end(true))
        {
            Serial.println("[OTA] ✅ Firmware flashed and signature verified successfully!");
            g_persistence.recordLastError("OTA_SUCCESS");
            _updateValid = true;
            otaReset();
            // Reboot is now safely handled by MicroOcpp's updateExecutable callback
            // to ensure standard-compliant status notifications.
        }
        else
        {
            Serial.printf("[OTA] ? Update.end failed: %s\n", Update.errorString());
            Update.abort();
            otaReset();
        }
    }

    bool OTAManager::checkUpdateSuccess()
    {
        const char *lastError = g_persistence.getLastError();
        return (strcmp(lastError, "OTA_SUCCESS") == 0);
    }

    bool OTAManager::isUpdateValid()
    {
        return _updateValid;
    }

    // ── Deferred reboot support ──────────────────────────────────────────
    // These are set by the OcppService install callback when the gun is
    // plugged during OTA. hw_svc_task polls hasDeferredReboot() and
    // reboots safely once the gun is unplugged.
    // VOLATILE: written by OCPP task (Core 0), read by hw_svc_task (Core 1).
    // Must be volatile to prevent the compiler from caching the value in a
    // CPU register — without this, Core 1 may never see the flag being set.
    static volatile bool     _deferredRebootPending = false;
    static volatile uint32_t _deferredSinceMs      = 0;

    void OTAManager::setDeferredReboot(bool pending)
    {
        _deferredRebootPending = pending;
        _deferredSinceMs      = pending ? (uint32_t)millis() : 0;
        if (pending) {
            Serial.println("[OTA] 🔄 Deferred reboot flag SET — waiting for gun unplug");
        } else {
            Serial.println("[OTA] 🔄 Deferred reboot flag CLEARED");
        }
    }

    bool OTAManager::hasDeferredReboot()
    {
        return _deferredRebootPending;
    }

    uint32_t OTAManager::getDeferredSinceMs()
    {
        return _deferredSinceMs;
    }

    OTAManager g_otaManager;
}
