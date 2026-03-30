#include "system/OtaManager.h"
#include "config/production_config.h"
#include "system/SecurityManager.h"
#include "services/OcppClient.h"
#include "config/version.h"
#include <Update.h>
#include <MicroOcpp/Core/Context.h>
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
    }

    static bool otaStart()
    {
        otaReset();
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

        size_t written = Update.write(writeBuf, writeLen);
        if (written != writeLen)
        {
            Serial.printf("[OTA] ? Write failed: %u/%u bytes\n", written, (unsigned)writeLen);
            return false;
        }

        writeLen = 0;
        return true;
    }

    static bool otaWriteOldest(uint8_t b)
    {
        if (mbedtls_sha256_update_ret(&shaCtx, &b, 1) != 0)
        {
            Serial.println("[OTA] ? SHA256 update failed");
            return false;
        }

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

        Serial.printf("[OTA] Progress: %u bytes\\n", Update.progress());
        return size;
    }

    void OTAManager::onDownloadComplete(int reason)
    {
        if (reason != 0)
        {
            Serial.printf("[OTA] ? Download failed (reason: %d)\n", reason);
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

        if (!g_securityManager.verifyOTASignature(hash, sizeof(hash), sig, sizeof(sig)))
        {
            Serial.println("[OTA] ? Signature verification failed");
            Update.abort();
            g_persistence.recordLastError("OTA_SIG_INVALID");
            ocpp::sendSystemAlert("OTA_SIGNATURE_INVALID", "Firmware signature check failed", "Critical");
            otaReset();
            return;
        }

        if (Update.end(true))
        {
            Serial.println("[OTA] ? Update complete! Rebooting...");
            g_persistence.recordLastError("OTA_SUCCESS");
            otaReset();
            delay(1000);
            ESP.restart();
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

    OTAManager g_otaManager;
}
