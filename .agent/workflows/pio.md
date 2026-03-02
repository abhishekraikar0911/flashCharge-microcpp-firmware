---
description: Build, Upload, and Monitor PlatformIO projects
---

## PlatformIO Utility Commands

This workflow provides commands to replicate the VS Code PlatformIO toolbar functionality (Build, Upload, Monitor).

### Common Commands

**// turbo**
1. **Build Firmware**
   ```powershell
   C:\Users\AKSHAY\.platformio\penv\Scripts\platformio.exe run -e charger_esp32_production
   ```

**// turbo**
2. **Upload Firmware**
   ```powershell
   C:\Users\AKSHAY\.platformio\penv\Scripts\platformio.exe run -t upload -e charger_esp32_production
   ```

**// turbo**
3. **Monitor Serial Output**
   ```powershell
   C:\Users\AKSHAY\.platformio\penv\Scripts\platformio.exe device monitor -b 115200
   ```

**// turbo**
4. **Upload and Monitor**
   ```powershell
   C:\Users\AKSHAY\.platformio\penv\Scripts\platformio.exe run -t upload -e charger_esp32_production; C:\Users\AKSHAY\.platformio\penv\Scripts\platformio.exe device monitor -b 115200
   ```

**// turbo**
5. **Clean Build Files**
   ```powershell
   C:\Users\AKSHAY\.platformio\penv\Scripts\platformio.exe run -t clean -e charger_esp32_production
   ```

## Usage
You can run these steps individually using the `run_command` tool.
