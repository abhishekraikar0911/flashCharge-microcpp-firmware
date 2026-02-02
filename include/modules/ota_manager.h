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
    };

    extern OTAManager g_otaManager;
}

#endif // OTA_MANAGER_H
