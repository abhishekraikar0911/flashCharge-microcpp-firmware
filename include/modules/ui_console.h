#pragma once

/**
 * @file ui_console.h
 * @brief Serial console UI for operator interaction
 * @author Rivot Motors
 * @date 2026
 */

#include <stdint.h>
#include <stdbool.h>

// ========== UI COMMANDS ==========

typedef enum
{
    UI_CMD_UNKNOWN = 0,
    UI_CMD_STATUS = 1,
    UI_CMD_START_CHARGE = 2,
    UI_CMD_STOP_CHARGE = 3,
    UI_CMD_RESET = 4,
    UI_CMD_DIAGNOSTICS = 5,
    UI_CMD_HELP = 6,
    UI_CMD_VERSION = 7,
    UI_CMD_CONFIG = 8,
    UI_CMD_LOGS = 9
} UiCommand;

// ========== UI INTERFACE ==========

namespace UI
{

    /**
     * @brief Initialize UI console
     * @param baud_rate Serial port baud rate
     * @return true if successful
     */
    bool init(uint32_t baud_rate = 115200);

    /**
     * @brief Process user input from serial console
     * Call this regularly from main loop
     */
    void update();

    /**
     * @brief Print status information to console
     */
    void printStatus();

    /**
     * @brief Print system diagnostics
     */
    void printDiagnostics();

    /**
     * @brief Print help menu
     */
    void printHelp();

    /**
     * @brief Print version information
     */
    void printVersion();

    /**
     * @brief Print formatted message to console
     * @param format Printf-style format string
     * @param ... Variable arguments
     */
    void println(const char *format, ...) __attribute__((format(printf, 1, 2)));

    /**
     * @brief Clear console screen
     */
    void clear();

    /**
     * @brief Set UI enabled/disabled
     * @param enable true to enable, false to disable
     */
    void setEnabled(bool enable);

    /**
     * @brief Check if UI is enabled
     * @return true if enabled
     */
    bool isEnabled();

    /**
     * @brief Get last command entered
     * @return Last command type
     */
    UiCommand getLastCommand();

} // namespace UI
