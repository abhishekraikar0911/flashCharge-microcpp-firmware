/**
 * @file SystemMonitor.h
 * @brief System monitor — pure coordinator, calls all services and watchdog
 * @layer Service
 *
 * SystemMonitor does NOT contain business logic.
 * Its only job is to call each service and kick the watchdog.
 */
#pragma once

namespace prod {

class SystemMonitor {
public:
    static SystemMonitor& instance() {
        static SystemMonitor inst;
        return inst;
    }

    void begin();
    void poll();

private:
    SystemMonitor() = default;
};

} // namespace prod
