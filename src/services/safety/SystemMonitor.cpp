#include "services/safety/SystemMonitor.h"
#include "services/charging/ChargerService.h"
#include "services/safety/SafetyService.h"
#include "services/charging/EnergyService.h"
#include "services/ui/LedService.h"
#include "services/network/NetworkService.h"
#include "services/safety/HealthMonitor.h"
#include "app/AppContext.h"

namespace prod {

void SystemMonitor::begin() {
    ChargerService::instance().begin();
    SafetyService::instance().begin();
    EnergyService::instance().begin();
    LedService::instance().begin();
    NetworkService::instance().begin();
    if (g_app.logger) g_app.logger->log(ILogger::Level::INFO, "SYS_MON", "All services initialized");
}

void SystemMonitor::poll() {
    uint32_t current_time = g_app.timer ? g_app.timer->millis() : 0;
    static uint32_t lastDiagLog = 0;
    bool shouldLog = (current_time - lastDiagLog > 5000);
    if (shouldLog) {
        lastDiagLog = current_time;
        if (g_app.logger) g_app.logger->log(ILogger::Level::DEBUG, "SYS_MON", "poll() starting cycle...");
    }

    ChargerService::instance().poll();
    if (shouldLog && g_app.logger) g_app.logger->log(ILogger::Level::DEBUG, "SYS_MON", "  ChargerService polled");
    
    SafetyService::instance().poll();
    if (shouldLog && g_app.logger) g_app.logger->log(ILogger::Level::DEBUG, "SYS_MON", "  SafetyService polled");
    
    EnergyService::instance().poll();
    if (shouldLog && g_app.logger) g_app.logger->log(ILogger::Level::DEBUG, "SYS_MON", "  EnergyService polled");
    
    LedService::instance().poll();
    if (shouldLog && g_app.logger) g_app.logger->log(ILogger::Level::DEBUG, "SYS_MON", "  LedService polled");
    
    NetworkService::instance().poll();
    if (shouldLog && g_app.logger) g_app.logger->log(ILogger::Level::DEBUG, "SYS_MON", "  NetworkService polled");

    // Kick watchdog
    prod::g_healthMonitor.feed();
}

} // namespace prod
