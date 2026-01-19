# Production Code Structure

This document describes the production-grade directory structure and organization.

## Directory Layout

```
microocpp/
├── include/
│   ├── config/                    # Configuration headers
│   │   ├── hardware.h            # Hardware definitions & pin mappings
│   │   ├── timing.h              # Timing constants & intervals
│   │   └── version.h             # Version information
│   ├── core/                      # Core system functionality
│   │   ├── logger.h              # Logging infrastructure
│   │   ├── event_system.h        # Event-driven architecture
│   │   └── system_health.h       # Health monitoring
│   ├── drivers/                   # Hardware drivers
│   │   ├── can_driver.h          # CAN/TWAI driver interface
│   │   ├── bms_interface.h       # BMS communication interface
│   │   └── charger_interface.h   # Charger communication interface
│   └── modules/                   # Functional modules
│       ├── ocpp_manager.h        # OCPP protocol implementation
│       ├── ui_console.h          # Serial console UI
│       └── ota_manager.h         # OTA update manager
├── src/
│   ├── config/                    # Configuration implementations
│   ├── core/                      # Core system implementations
│   │   ├── logger.cpp
│   │   ├── event_system.cpp
│   │   └── system_health.cpp
│   ├── drivers/                   # Hardware driver implementations
│   │   ├── can_driver.cpp
│   │   ├── bms_interface.cpp
│   │   └── charger_interface.cpp
│   ├── modules/                   # Functional module implementations
│   │   ├── ocpp_manager.cpp
│   │   ├── ui_console.cpp
│   │   └── ota_manager.cpp
│   └── main.cpp                   # Application entry point
├── lib/                           # Third-party dependencies
│   └── MicroOcpp/                # OCPP protocol library
├── docs/                          # Documentation
├── platformio.ini                 # Build configuration
└── README.md                      # Project overview
```

## Module Responsibilities

### Config (`include/config/`)
- Hardware pin definitions and constraints
- System timing parameters and intervals
- Feature flags and compilation options
- Version and build information

### Core (`include/core/`)
- **Logger**: Structured logging with levels and tags
- **EventSystem**: Inter-module communication via events
- **SystemHealth**: Health monitoring and diagnostics

### Drivers (`include/drivers/`)
Hardware abstraction layer:
- **CAN Driver**: ESP32 TWAI bus communication (250kbps)
- **BMS Interface**: Battery state, voltage, temperature, SOC
- **Charger Interface**: Power output, status, control

### Modules (`include/modules/`)
Application-level functionality:
- **OCPP Manager**: Charge point protocol, transactions, meter values
- **UI Console**: Serial interface, status display, commands
- **OTA Manager**: Firmware updates and verification

## Build Process

1. **Compilation**: All source files in `src/` are compiled to object files
2. **Linking**: Objects linked with MicroOcpp library and Arduino core
3. **Flash**: Firmware image programmed to ESP32 flash memory

See `platformio.ini` for specific build configuration.

## Namespace Organization

All functions are organized within namespaces:

```cpp
namespace CAN { ... }
namespace BMS { ... }
namespace Charger { ... }
namespace OCPP { ... }
namespace UI { ... }
namespace OTA { ... }
namespace Logger { ... }
namespace EventSystem { ... }
namespace SystemHealth { ... }
```

This prevents naming conflicts and improves code organization.

## Header Guards and Includes

All headers use `#pragma once` for include guards (modern, standard approach).

Include order (when needed):
1. Standard C/C++ headers (`<stdio.h>`, `<stdint.h>`)
2. Arduino headers (`<Arduino.h>`)
3. Third-party headers (`<driver/twai.h>`)
4. Project headers (`"config/hardware.h"`)

## Coding Standards

- **Naming**: 
  - Classes/structs: `PascalCase`
  - Functions: `camelCase`
  - Constants: `UPPER_SNAKE_CASE`
  - Members: `snake_case`

- **Comments**: Doxygen-style for all public APIs
  - `@brief`: One-line description
  - `@param`: Parameter descriptions
  - `@return`: Return value description
  
- **Error Handling**: Functions return `bool` for success/failure status

- **Logging**: Use `LOG_TAG_*` macros for consistent logging

## Build Configuration

See [platformio.ini](../platformio.ini) for:
- Compiler flags and optimizations
- Include paths and source file filters
- Board and platform selection
- Library dependencies and versions

## Testing

Test-related files:
- Unit tests for each module
- Integration tests for system functionality
- CI/CD pipeline configuration

## Production Deployment

For production builds:

1. Set appropriate optimization flags in `platformio.ini`
2. Verify all logging is at appropriate levels
3. Run full system tests
4. Generate documentation
5. Create firmware release archive
6. Update CHANGELOG.md with release notes

## Version Management

Versions follow [Semantic Versioning](https://semver.org/):

- **MAJOR.MINOR.PATCH**
- Increment MAJOR for breaking changes
- Increment MINOR for new features
- Increment PATCH for bug fixes

Update version in `include/config/version.h` for each release.

## Continuous Integration

Build validation:
- Clean compilation check
- Warning elimination
- Static code analysis (where applicable)
- Firmware size tracking
- Performance benchmarks

## Support and Documentation

- `docs/`: Project documentation
- `PRODUCTION_GUIDE.md`: Deployment guide
- `PRODUCTION_UPGRADES.md`: Upgrade procedures
- Source code comments: Implementation details
