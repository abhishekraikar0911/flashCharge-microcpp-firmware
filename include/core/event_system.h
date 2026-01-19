#pragma once

/**
 * @file event_system.h
 * @brief Event-driven architecture for system communication
 * @author Rivot Motors
 * @date 2026
 */

#include <stdint.h>
#include <functional>

// ========== EVENT TYPES ==========

typedef enum
{
    // CAN Events
    EVENT_CAN_INITIALIZED,
    EVENT_CAN_ERROR,
    EVENT_CAN_MESSAGE_RECEIVED,

    // BMS Events
    EVENT_BMS_VOLTAGE_WARNING,
    EVENT_BMS_TEMPERATURE_WARNING,
    EVENT_BMS_SOC_UPDATE,
    EVENT_BMS_COMMUNICATION_LOST,

    // Charger Events
    EVENT_CHARGER_ENABLED,
    EVENT_CHARGER_DISABLED,
    EVENT_CHARGER_FAULT,
    EVENT_CHARGER_COMMUNICATION_LOST,

    // OCPP Events
    EVENT_OCPP_CONNECTED,
    EVENT_OCPP_DISCONNECTED,
    EVENT_OCPP_TRANSACTION_STARTED,
    EVENT_OCPP_TRANSACTION_STOPPED,

    // System Events
    EVENT_SYSTEM_RESET,
    EVENT_SYSTEM_SHUTDOWN,
    EVENT_SYSTEM_ERROR,
    EVENT_WATCHDOG_TRIGGERED,

    EVENT_MAX
} SystemEventType;

// ========== EVENT DATA ==========

struct SystemEvent
{
    SystemEventType type;
    uint32_t timestamp_ms;
    uint32_t source_id;
    int32_t data1;
    int32_t data2;
    void *user_data;
};

// ========== EVENT HANDLER TYPEDEF ==========

typedef std::function<void(const SystemEvent &)> EventHandler;

// ========== EVENT SYSTEM INTERFACE ==========

namespace EventSystem
{

    /**
     * @brief Initialize event system
     */
    void init();

    /**
     * @brief Subscribe to system event
     * @param event_type Event type to subscribe to
     * @param handler Callback function for event
     * @return Subscription ID (use for unsubscribe)
     */
    uint32_t subscribe(SystemEventType event_type, EventHandler handler);

    /**
     * @brief Unsubscribe from event
     * @param subscription_id ID returned from subscribe()
     */
    void unsubscribe(uint32_t subscription_id);

    /**
     * @brief Post event to system
     * @param event Event structure with data
     */
    void post(const SystemEvent &event);

    /**
     * @brief Flush all pending events
     */
    void flush();

    /**
     * @brief Get event count for monitoring
     * @return Number of queued events
     */
    uint32_t getQueuedEventCount();

} // namespace EventSystem
