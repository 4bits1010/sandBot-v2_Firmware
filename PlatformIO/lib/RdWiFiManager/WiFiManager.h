// WiFi Manager
// Rob Dobson 2018

#pragma once

#include <Arduino.h>
#include "WiFi.h"
#include <DNSServer.h>

class StatusIndicator;
class ConfigBase;

class WiFiManager
{
private:
    bool _wifiEnabled;
    String _ssid;
    String _password;
    static String _hostname;
    String _defaultHostname;
    unsigned long _lastWifiBeginAttemptMs;
    bool _wifiFirstBeginDone;
    static constexpr unsigned long TIME_BETWEEN_WIFI_BEGIN_ATTEMPTS_MS = 60000;
    static constexpr unsigned long TIME_BEFORE_FIRST_BEGIN_MS = 2000;
    ConfigBase* _pConfigBase;
    static StatusIndicator* _pStatusLed;
    static WiFiManager* _pInstance;
    // Reset
    bool _deviceRestartPending;
    unsigned long _deviceRestartMs;
    static const int DEVICE_RESTART_DELAY_MS = 1000;
    
    // Portal mode
    bool _portalMode;
    unsigned long _portalStartTime;
    int _connectionFailures;
    static const int MAX_CONNECTION_FAILURES = 3;
    static const unsigned long PORTAL_TIMEOUT_MS = 0; // Disabled - portal stays active until credentials saved
    static const unsigned long CONNECTION_TIMEOUT_MS = 30000; // 30 seconds to connect
    String _apSSID;
    DNSServer _dnsServer;
    unsigned long _connectionAttemptStartTime;

public:
    WiFiManager()
    {
        _wifiEnabled = false;
        _lastWifiBeginAttemptMs = 0;
        _wifiFirstBeginDone = false;
        _pConfigBase = NULL;
        _pStatusLed = NULL;
        _deviceRestartPending = false;
        _deviceRestartMs = 0;
        _portalMode = false;
        _portalStartTime = 0;
        _connectionFailures = 0;
        _apSSID = "";
        _connectionAttemptStartTime = 0;
    }

    bool isEnabled();
    String getHostname();
    void setup(ConfigBase& hwConfig, ConfigBase *pSysConfig, const char *defaultHostname, StatusIndicator *pStatusLed);
    void service();
    bool isConnected();
    String formConfigStr();
    void setCredentials(String &ssid, String &pw, String &hostname, bool resetToImplement);
    void clearCredentials();
    static void wiFiEventHandler(WiFiEvent_t event);
    static const char* getEventName(WiFiEvent_t event);
    
    // Portal mode methods
    bool isPortalMode();
    void startPortalMode();
    void stopPortalMode();
    bool shouldStartPortal();
    String getPortalSSID();
    String getPortalHTML();
    
    // SD Card network configuration
    bool loadNetworkConfigFromSD(class FileManager& fileManager);
};
