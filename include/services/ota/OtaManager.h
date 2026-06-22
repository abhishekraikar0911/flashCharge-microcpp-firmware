#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>
#include <Update.h>

namespace prod
{
    class OTAManager
    {
    public:
        void init();
        
        // Called by MicroOcpp when firmware download starts
        static size_t onFirmwareData(const unsigned char *buf, size_t size);
        
        // Called when download completes/fails
        static void onDownloadComplete(int reason);
        
        // Check if update was successful after reboot
        static bool checkUpdateSuccess();

        // Check if the freshly downloaded update is valid and ready to execute
        static bool isUpdateValid();

        // ── Deferred reboot support ──────────────────────────────────────
        // When OTA install is triggered but gun is plugged, we defer the
        // reboot instead of rejecting. hw_svc_task polls hasDeferredReboot()
        // and reboots automatically once the gun is safely unplugged.

        /** Set or clear the deferred reboot flag. */
        static void setDeferredReboot(bool pending);

        /** Returns true if a firmware install is waiting for gun unplug. */
        static bool hasDeferredReboot();

        /** Returns millis() timestamp of when the deferral was set. */
        static uint32_t getDeferredSinceMs();
    };

    extern OTAManager g_otaManager;
}

#endif // OTA_MANAGER_H

