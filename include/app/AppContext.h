/**
 * @file AppContext.h
 * @brief Global dependency registry containing all HAL and Driver interfaces
 * @layer Application
 *
 * Populated once dynamically by the active BSP during boot.
 * Used by all application components via `extern AppContext g_app;`
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once

#include "hal/interfaces/IFlash.h"
#include "hal/interfaces/ITimer.h"
#include "hal/interfaces/IWatchdog.h"
#include "hal/interfaces/ILogger.h"
#include "hal/interfaces/IGpio.h"
#include "system/IConfig.h"
#include "drivers/interfaces/IChargerModule.h"
#include "drivers/interfaces/IBms.h"
#include "drivers/interfaces/IModem.h"
#include "drivers/interfaces/IRelay.h"
#include "drivers/interfaces/ISensor.h"

struct AppContext {
    // ---- HAL (MCU Abstractions) ----
    IFlash*         flash      = nullptr;
    ITimer*         timer      = nullptr;
    IWatchdog*      wdt        = nullptr;
    ILogger*        logger     = nullptr;

    // ---- System Layer ----
    IConfig*        config     = nullptr;

    // ---- Device Drivers ----
    IChargerModule* charger    = nullptr;
    IBms*           bms        = nullptr;
    IModem*         modem      = nullptr;
    IRelay*         relay      = nullptr;
    ISensor*        tempSensor = nullptr;
    IGpio*          gpio       = nullptr;
};

// Global context variable. Defines the single point of truth for hardware linkage.
extern AppContext g_app;
