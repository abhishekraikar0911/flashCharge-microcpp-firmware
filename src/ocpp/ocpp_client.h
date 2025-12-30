#pragma once
#include <Arduino.h>
#pragma once
#ifndef OCPP_CLIENT_H
#define OCPP_CLIENT_H

#include <Arduino.h>

// Start the OCPP / telemetry client (creates a FreeRTOS task)
void startOCPP();

// Send a single telemetry packet immediately (public API)
void ocpp_sendTelemetry();

#endif // OCPP_CLIENT_H