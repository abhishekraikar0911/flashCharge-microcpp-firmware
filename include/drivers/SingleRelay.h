/**
 * @file SingleRelay.h
 * @brief Hardware-independent driver for a single contactor/relay
 * @layer Device Driver
 *
 * Implements IRelay using an injected IGpio interface.
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include "drivers/interfaces/IRelay.h"
#include "hal/interfaces/IGpio.h"

class SingleRelay : public IRelay {
public:
    /**
     * @param gpio  Injected GPIO HAL instance
     * @param pin   Physical pin number
     * @param activeHigh True if writing HIGH closes the relay
     */
    SingleRelay(IGpio& gpio, int pin, bool activeHigh = true);
    virtual ~SingleRelay() = default;

    void init();
    void close() override;
    void open() override;
    bool isClosed() override;

private:
    IGpio& gpio;
    int pin;
    bool activeHigh;
    bool closedState;
};
