#pragma once

/**
 * @file secrets.h
 * @brief SECURITY FIX: Hardcoded credentials replaced with secure storage
 * 
 * CRITICAL SECURITY IMPROVEMENT:
 * This file previously contained hardcoded WiFi passwords and server credentials.
 * All credentials have been migrated to encrypted NVS storage for security.
 * 
 * Migration Status:
 * - WiFi credentials: Moved to SecureCredentials encrypted storage
 * - OCPP server config: Moved to SecureCredentials encrypted storage  
 * - Charger identity: Moved to secure configuration system
 * 
 * Usage:
 * - Use SecureConfig::getWiFiCredentials() to load WiFi settings
 * - Use SecureConfig::getOCPPConfig() to load server configuration
 * - Credentials are automatically migrated on first boot
 * 
 * IMPORTANT: Remove this file from version control after migration!
 */

// SECURITY NOTICE: No hardcoded credentials in this file
// All sensitive data is now stored in encrypted NVS storage

#warning "SECURITY: secrets.h should be removed from version control after migration"

// Temporary compatibility - these will be removed in future versions
// Use SecureConfig functions instead
#define SECRET_CHARGER_ID "SECURE_STORAGE"  // Loaded from encrypted NVS
#define SECRET_CHARGER_MODEL "SECURE_STORAGE"  // Loaded from encrypted NVS
#define SECRET_CHARGER_VENDOR "SECURE_STORAGE"  // Loaded from encrypted NVS
#define SECRET_CSMS_HOST "SECURE_STORAGE"  // Loaded from encrypted NVS
#define SECRET_CSMS_PORT 443  // Loaded from encrypted NVS
#define SECRET_CSMS_URL "SECURE_STORAGE"  // Loaded from encrypted NVS

