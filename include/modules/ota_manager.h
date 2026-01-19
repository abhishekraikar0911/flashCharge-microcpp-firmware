#pragma once

/**
 * @file ota_manager.h
 * @brief Over-The-Air firmware update management
 * @author Rivot Motors
 * @date 2026
 */

#include <stdint.h>
#include <stdbool.h>

// ========== OTA STATUS ==========

typedef enum
{
    OTA_IDLE = 0,
    OTA_CHECKING = 1,
    OTA_DOWNLOADING = 2,
    OTA_INSTALLING = 3,
    OTA_COMPLETE = 4,
    OTA_ERROR = 5
} OtaStatus;

// ========== OTA INFO ==========

struct OtaInfo
{
    OtaStatus status;
    uint32_t progress_percent;
    uint32_t downloaded_bytes;
    uint32_t total_bytes;
    const char *error_message;
    uint32_t start_time_ms;
};

// ========== OTA MANAGER INTERFACE ==========

namespace OTA
{

    /**
     * @brief Initialize OTA manager
     * @return true if successful
     */
    bool init();

    /**
     * @brief Check for firmware updates
     * @param update_server URL to check for updates
     * @return true if check initiated
     */
    bool checkForUpdates(const char *update_server);

    /**
     * @brief Download and install firmware update
     * @param firmware_url URL of firmware file
     * @return true if update initiated
     */
    bool startUpdate(const char *firmware_url);

    /**
     * @brief Get OTA status
     * @return Current OTA operation status
     */
    OtaInfo getStatus();

    /**
     * @brief Cancel ongoing update
     * @return true if cancelled successfully
     */
    bool cancel();

    /**
     * @brief Verify downloaded firmware
     * @param checksum Expected SHA256 checksum
     * @return true if verification passed
     */
    bool verifyFirmware(const char *checksum);

    /**
     * @brief Perform OTA update processing
     * Call this regularly to process download and installation
     */
    void update();

    /**
     * @brief Get available firmware version
     * @return Version string
     */
    const char *getAvailableVersion();

    /**
     * @brief Get current firmware version
     * @return Version string
     */
    const char *getCurrentVersion();

} // namespace OTA
