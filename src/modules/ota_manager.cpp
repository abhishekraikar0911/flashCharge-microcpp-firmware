#include "../../include/modules/ota_manager.h"
#include "../../include/production_config.h"
#include "../../include/header.h"
#include <Update.h>
#include <MicroOcpp/Core/Context.h>
#include <MicroOcpp/Model/Model.h>
#include <MicroOcpp/Model/FirmwareManagement/FirmwareService.h>

namespace prod
{
    void OTAManager::init()
    {
        Serial.println("[OTA] 🔄 OTA Manager initialized");
        
        // Check if previous update was successful
        if (checkUpdateSuccess())
        {
            Serial.println("[OTA] ✅ Previous firmware update successful");
        }
    }

    size_t OTAManager::onFirmwareData(const unsigned char *buf, size_t size)
    {
        // First chunk - begin update
        if (Update.isRunning() == false)
        {
            Serial.printf("[OTA] 📦 Starting update (size: %u bytes)\n", UPDATE_SIZE_UNKNOWN);
            
            if (!Update.begin(UPDATE_SIZE_UNKNOWN))
            {
                Serial.printf("[OTA] ❌ Update.begin failed: %s\n", Update.errorString());
                return 0;
            }
        }

        // Write chunk
        size_t written = Update.write((uint8_t*)buf, size);
        if (written != size)
        {
            Serial.printf("[OTA] ❌ Write failed: %u/%u bytes\n", written, size);
            return 0;
        }

        Serial.printf("[OTA] 📝 Written: %u bytes (total: %u)\n", written, Update.progress());
        return written;
    }

    void OTAManager::onDownloadComplete(int reason)
    {
        if (reason == 0) // Success
        {
            if (Update.end(true))
            {
                Serial.println("[OTA] ✅ Update complete! Rebooting...");
                g_persistence.recordLastError("OTA_SUCCESS");
                delay(1000);
                ESP.restart();
            }
            else
            {
                Serial.printf("[OTA] ❌ Update.end failed: %s\n", Update.errorString());
            }
        }
        else
        {
            Serial.printf("[OTA] ❌ Download failed (reason: %d)\n", reason);
            Update.abort();
        }
    }

    bool OTAManager::checkUpdateSuccess()
    {
        const char *lastError = g_persistence.getLastError();
        return (strcmp(lastError, "OTA_SUCCESS") == 0);
    }

    OTAManager g_otaManager;
}
