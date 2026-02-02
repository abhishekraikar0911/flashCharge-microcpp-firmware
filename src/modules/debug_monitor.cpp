#include "../../include/debug_monitor.h"

LogLevel DebugMonitor::currentLevel = LOG_INFO;
bool DebugMonitor::enableColors = true;
bool DebugMonitor::enableTimestamp = true;
unsigned long DebugMonitor::startTime = 0;

DebugMonitor Debug;
