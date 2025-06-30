// WiFi Manager
// Rob Dobson 2018

#include "WiFiManager.h"
#include <ArduinoLog.h>
#include <WiFi.h>
#include "ConfigNVS.h"
#include "StatusIndicator.h"
#include <ESPmDNS.h>
#include "RdJson.h"
#include "FileManager.h"

static const char* MODULE_PREFIX = "WiFiManager: ";

StatusIndicator *WiFiManager::_pStatusLed = NULL;
WiFiManager* WiFiManager::_pInstance = NULL;

String WiFiManager::_hostname;

bool WiFiManager::isEnabled()
{
    return _wifiEnabled;
}

String WiFiManager::getHostname()
{
    return _hostname;
}

void WiFiManager::setup(ConfigBase& hwConfig, ConfigBase *pSysConfig, 
            const char *defaultHostname, StatusIndicator *pStatusLed)
{
    _wifiEnabled = hwConfig.getLong("wifiEnabled", 0) != 0;
    _pConfigBase = pSysConfig;
    _defaultHostname = defaultHostname;
    _pStatusLed = pStatusLed;
    _pInstance = this;
    // Get the SSID, password and hostname if available
    _ssid = pSysConfig->getString("WiFiSSID", "");
    _password = pSysConfig->getString("WiFiPW", "");
    _hostname = pSysConfig->getString("WiFiHostname", _defaultHostname.c_str());
    
    // Reset connection failure state on setup to ensure clean state after system reset
    _connectionFailures = 0;
    _connectionAttemptStartTime = 0;
    _lastWifiBeginAttemptMs = 0;
    
    Log.notice("%sWiFi setup - SSID: %s, Hostname: %s, Credentials present: %s\n", MODULE_PREFIX,
               _ssid.length() > 0 ? _ssid.c_str() : "(none)",
               _hostname.c_str(),
               (_ssid.length() > 0) ? "YES" : "NO");
    
    // Generate AP SSID for portal mode
    String macAddr = WiFi.macAddress();
    macAddr.replace(":", "");
    _apSSID = "sandBot-" + macAddr.substring(6);
    
    // Set an event handler for WiFi events
    if (_wifiEnabled)
    {
        WiFi.onEvent(wiFiEventHandler);
        // Set the mode to STA initially
        WiFi.mode(WIFI_STA);
        
        // Portal mode decision deferred to main.cpp after all credential sources are checked
    }
}

void WiFiManager::service()
{
    // Check enabled
    if (!_wifiEnabled)
        return;

    // Check restart pending
    if (_deviceRestartPending)
    {
        if (Utils::isTimeout(millis(), _deviceRestartMs, DEVICE_RESTART_DELAY_MS))
        {
            _deviceRestartPending = false;
            ESP.restart();
        }
    }

    // Handle portal mode
    if (_portalMode)
    {
        // Process DNS requests for captive portal
        _dnsServer.processNextRequest();
        
        // Check for portal timeout (only if timeout is enabled)
        if (PORTAL_TIMEOUT_MS > 0 && Utils::isTimeout(millis(), _portalStartTime, PORTAL_TIMEOUT_MS))
        {
            Log.notice("%sPortal timeout, stopping portal mode\n", MODULE_PREFIX);
            stopPortalMode();
        }
        return; // Don't try to connect while in portal mode
    }

    // Check for reconnect required
    if (WiFi.status() != WL_CONNECTED)
    {
        // Check if current connection attempt has timed out
        if (_connectionAttemptStartTime > 0 && 
            Utils::isTimeout(millis(), _connectionAttemptStartTime, CONNECTION_TIMEOUT_MS))
        {
            Log.notice("%sConnection timeout after %dms, attempt failed\n", MODULE_PREFIX, CONNECTION_TIMEOUT_MS);
            _connectionFailures++;
            _connectionAttemptStartTime = 0; // Reset attempt time
            
            if (_connectionFailures >= MAX_CONNECTION_FAILURES)
            {
                Log.notice("%sMax connection failures (%d) reached, starting portal mode\n", 
                          MODULE_PREFIX, MAX_CONNECTION_FAILURES);
                startPortalMode();
                return;
            }
        }
        
        if (Utils::isTimeout(millis(), _lastWifiBeginAttemptMs, 
                    _wifiFirstBeginDone ? TIME_BETWEEN_WIFI_BEGIN_ATTEMPTS_MS : TIME_BEFORE_FIRST_BEGIN_MS))
        {
            // Check if we have credentials
            if (_ssid.length() == 0)
            {
                Log.notice("%sNo WiFi credentials, starting portal mode\n", MODULE_PREFIX);
                startPortalMode();
                return;
            }
            
            Log.notice("%snotConn WiFi.begin SSID %s (attempt %d)\n", MODULE_PREFIX, _ssid.c_str(), _connectionFailures + 1);
            WiFi.begin(_ssid.c_str(), _password.c_str());
            WiFi.setHostname(_hostname.c_str());
            _lastWifiBeginAttemptMs = millis();
            _connectionAttemptStartTime = millis(); // Start tracking this attempt
            _wifiFirstBeginDone = true;
        }
    }
    else
    {
        // Reset failure count and attempt time on successful connection
        _connectionFailures = 0;
        _connectionAttemptStartTime = 0;
    }
}

bool WiFiManager::isConnected()
{
    return (WiFi.status() == WL_CONNECTED);
}

String WiFiManager::formConfigStr()
{
    return "{\"WiFiSSID\":\"" + _ssid + "\",\"WiFiPW\":\"" + _password + "\",\"WiFiHostname\":\"" + _hostname + "\"}";
}

void WiFiManager::setCredentials(String &ssid, String &pw, String &hostname, bool resetToImplement)
{
    // Set credentials
    _ssid = ssid;
    _password = pw;
    if (hostname.length() == 0)
        Log.trace("%shostname not set, staying with %s\n", MODULE_PREFIX, _hostname.c_str());
    else
        _hostname = hostname;
    if (_pConfigBase)
    {
        _pConfigBase->setConfigData(formConfigStr().c_str());
        _pConfigBase->writeConfig();
    }

    // Exit portal mode if we were in it
    if (_portalMode)
    {
        Log.notice("%sNew credentials set, exiting portal mode\n", MODULE_PREFIX);
        stopPortalMode();
    }

    // Check if reset required
    if (resetToImplement)
    {
        Log.trace("%ssetCredentials ... Reset pending\n", MODULE_PREFIX);
        _deviceRestartPending = true;
        _deviceRestartMs = millis();
    }

}

void WiFiManager::clearCredentials()
{
    Log.notice("%sClearing WiFi credentials from NVS\n", MODULE_PREFIX);
    _ssid = "";
    _password = "";
    _hostname = _defaultHostname;
    if (_pConfigBase)
    {
        _pConfigBase->setConfigData(formConfigStr().c_str());
        _pConfigBase->writeConfig();
        Log.notice("%sWiFi credentials cleared, config written to NVS\n", MODULE_PREFIX);
    }
    
    // Disconnect current WiFi and reset failure count
    WiFi.disconnect();
    _connectionFailures = 0;
    _connectionAttemptStartTime = 0;
    
    // Start portal mode since we no longer have credentials
    Log.notice("%sStarting portal mode after credential clear\n", MODULE_PREFIX);
    startPortalMode();
}

void WiFiManager::wiFiEventHandler(WiFiEvent_t event)
{
    Log.trace("%sEvent %s\n", MODULE_PREFIX, getEventName(event));
    switch (event)
    {
    case SYSTEM_EVENT_STA_GOT_IP:
        Log.notice("%sGotIP %s (uptime: %ums)\n", MODULE_PREFIX, WiFi.localIP().toString().c_str(), (unsigned int)millis());
            // Reset connection failure count and attempt timer on successful connection
        if (_pInstance)
        {
            _pInstance->_connectionFailures = 0;
            _pInstance->_connectionAttemptStartTime = 0;
        }
        if (_pStatusLed)
            _pStatusLed->setCode(1);
        //
        // Set up mDNS responder:
        // - first argument is the domain name, in this example
        //   the fully-qualified domain name is "esp8266.local"
        // - second argument is the IP address to advertise
        //   we send our IP address on the WiFi network
        if (MDNS.begin(_hostname.c_str()))
        {
            Log.notice("%smDNS responder started with hostname %s\n", MODULE_PREFIX, _hostname.c_str());
        }
        else
        {
            Log.notice("%smDNS responder failed to start (hostname %s)\n", MODULE_PREFIX, _hostname.c_str());
            break;
        }
        // Add service to MDNS-SD
        MDNS.addService("http", "tcp", 80);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        {
            wl_status_t status = WiFi.status();
            Log.notice("%sDisconnected (status: %d) (uptime: %ums)\n", MODULE_PREFIX, status, (unsigned int)millis());
            
            // Check for authentication/connection failures that should trigger portal
            if (_pInstance && !_pInstance->_portalMode)
            {
                bool shouldCountFailure = false;
                
                switch (status) 
                {
                    case WL_NO_SSID_AVAIL:
                        Log.notice("%sSSID not found\n", MODULE_PREFIX);
                        shouldCountFailure = true;
                        break;
                    case WL_CONNECT_FAILED:
                        Log.notice("%sConnection failed\n", MODULE_PREFIX);
                        shouldCountFailure = true;
                        break;
                    case WL_CONNECTION_LOST:
                        Log.notice("%sConnection lost\n", MODULE_PREFIX);
                        // Don't count as failure - this is normal disconnection
                        break;
                    case WL_DISCONNECTED:
                        Log.notice("%sDisconnected status - checking if during connection attempt\n", MODULE_PREFIX);
                        // Only count as failure if disconnected within 5s of connection attempt
                        if (_pInstance->_connectionAttemptStartTime > 0 && 
                            Utils::isTimeout(millis(), _pInstance->_connectionAttemptStartTime, 5000))
                        {
                            Log.notice("%sDisconnected within 5s of connection attempt - likely auth failure\n", MODULE_PREFIX);
                            shouldCountFailure = true;
                        }
                        else
                        {
                            Log.notice("%sDisconnected but no recent connection attempt - normal disconnection\n", MODULE_PREFIX);
                        }
                        break;
                    default:
                        Log.notice("%sDisconnected with status %d\n", MODULE_PREFIX, status);
                        // Only count unknown statuses as failures if during recent connection attempt
                        if (_pInstance->_connectionAttemptStartTime > 0 &&
                            Utils::isTimeout(millis(), _pInstance->_connectionAttemptStartTime, 5000))
                        {
                            shouldCountFailure = true;
                        }
                        break;
                }
                
                if (shouldCountFailure)
                {
                    _pInstance->_connectionFailures++;
                    _pInstance->_connectionAttemptStartTime = 0; // Reset attempt timer
                    
                    // Check if max failures reached
                    if (_pInstance->_connectionFailures >= MAX_CONNECTION_FAILURES)
                    {
                        Log.notice("%sMax connection failures (%d) reached, starting portal mode\n", 
                                  MODULE_PREFIX, MAX_CONNECTION_FAILURES);
                        _pInstance->startPortalMode();
                        break; // Don't call WiFi.reconnect() if starting portal
                    }
                }
            }
            
            // Only try to reconnect if not in portal mode
            if (!_pInstance->_portalMode) {
                WiFi.reconnect();
            }
            if (_pStatusLed)
                _pStatusLed->setCode(0);
        }
        break;
    default:
        // INFO: Default = do nothing
        // Log.notice("WiFiManager: unknown event %d\n", event);
        break;
    }
}

const char* WiFiManager::getEventName(WiFiEvent_t event)
{
    static const char* sysEventNames [] {
        "SYSTEM_EVENT_WIFI_READY",           
        "SYSTEM_EVENT_SCAN_DONE",                
        "SYSTEM_EVENT_STA_START",                
        "SYSTEM_EVENT_STA_STOP",                 
        "SYSTEM_EVENT_STA_CONNECTED",            
        "SYSTEM_EVENT_STA_DISCONNECTED",         
        "SYSTEM_EVENT_STA_AUTHMODE_CHANGE",      
        "SYSTEM_EVENT_STA_GOT_IP",               
        "SYSTEM_EVENT_STA_LOST_IP",              
        "SYSTEM_EVENT_STA_WPS_ER_SUCCESS",       
        "SYSTEM_EVENT_STA_WPS_ER_FAILED",        
        "SYSTEM_EVENT_STA_WPS_ER_TIMEOUT",       
        "SYSTEM_EVENT_STA_WPS_ER_PIN",           
        "SYSTEM_EVENT_AP_START",                 
        "SYSTEM_EVENT_AP_STOP",                  
        "SYSTEM_EVENT_AP_STACONNECTED",          
        "SYSTEM_EVENT_AP_STADISCONNECTED",       
        "SYSTEM_EVENT_AP_STAIPASSIGNED",         
        "SYSTEM_EVENT_AP_PROBEREQRECVED",        
        "SYSTEM_EVENT_GOT_IP6",                 
        "SYSTEM_EVENT_ETH_START",                
        "SYSTEM_EVENT_ETH_STOP",                 
        "SYSTEM_EVENT_ETH_CONNECTED",            
        "SYSTEM_EVENT_ETH_DISCONNECTED",         
        "SYSTEM_EVENT_ETH_GOT_IP"
        };

    //if (event < 0 || event > SYSTEM_EVENT_MAX)
    int eventValue = static_cast<int>(event);
    if (eventValue < 0 || eventValue > static_cast<int>(SYSTEM_EVENT_MAX))
    {
        return "UNKNOWN WiFi event";
    }
    return sysEventNames[event];
}

// Portal mode methods
bool WiFiManager::isPortalMode()
{
    return _portalMode;
}

void WiFiManager::startPortalMode()
{
    if (_portalMode)
        return; // Already in portal mode
        
    Log.notice("%sStarting WiFi portal mode with SSID: %s\n", MODULE_PREFIX, _apSSID.c_str());
    
    // Stop any ongoing WiFi connection
    WiFi.disconnect();
    
    // Start AP mode with password
    WiFi.mode(WIFI_AP);
    const char* apPassword = "SecureThis";  // Default softAP password for setup
    WiFi.softAP(_apSSID.c_str(), apPassword);
    
    // Start captive portal DNS server
    _dnsServer.start(53, "*", WiFi.softAPIP());
    
    _portalMode = true;
    _portalStartTime = millis();
    
    // Update status LED
    if (_pStatusLed)
        _pStatusLed->setCode(2); // Different code for portal mode
        
    Log.notice("%sWiFi Portal started - Connect to %s (password: %s) and visit http://192.168.4.1\n", 
               MODULE_PREFIX, _apSSID.c_str(), apPassword);
}

void WiFiManager::stopPortalMode()
{
    if (!_portalMode)
        return;
        
    Log.notice("%sStopping WiFi portal mode\n", MODULE_PREFIX);
    
    // Stop DNS server
    _dnsServer.stop();
    
    _portalMode = false;
    _connectionFailures = 0; // Reset failure count
    
    // Switch back to STA mode
    WiFi.mode(WIFI_STA);
    
    // Update status LED
    if (_pStatusLed)
        _pStatusLed->setCode(0);
}

bool WiFiManager::shouldStartPortal()
{
    // Start portal if no credentials are stored
    return (_ssid.length() == 0);
}

String WiFiManager::getPortalSSID()
{
    return _apSSID;
}

// SD Card network configuration
bool WiFiManager::loadNetworkConfigFromSD(FileManager& fileManager)
{
    // Check if SD card is available and mounted
    if (!fileManager.isSDCardOk())
    {
        Log.trace("%sSD card not available for network config\n", MODULE_PREFIX);
        return false;
    }
    
    // Try to read the .network file from SD card root
    String networkFileContents = fileManager.getFileContents("sd", ".network", 2048);
    if (networkFileContents.length() == 0)
    {
        Log.trace("%sNo .network file found on SD card\n", MODULE_PREFIX);
        return false;
    }
    
    Log.notice("%sFound .network file on SD card, parsing...\n", MODULE_PREFIX);
    Log.trace("%sNetwork file contents: %s\n", MODULE_PREFIX, networkFileContents.c_str());
    
    // Parse JSON
    String wifiMode = RdJson::getString("wifi", "", networkFileContents.c_str());
    wifiMode.toLowerCase();
    
    if (wifiMode == "yes")
    {
        // Extract WiFi credentials
        String sdSSID = RdJson::getString("WiFiSSID", "", networkFileContents.c_str());
        String sdPassword = RdJson::getString("WiFiPW", "", networkFileContents.c_str());
        String sdHostname = RdJson::getString("WiFiHostname", "", networkFileContents.c_str());
        
        if (sdSSID.length() > 0)
        {
            Log.notice("%sSD card WiFi config: SSID=%s, hostname=%s\n", MODULE_PREFIX, 
                      sdSSID.c_str(), sdHostname.length() > 0 ? sdHostname.c_str() : "(default)");
            
            // Set credentials and save to NVS
            _ssid = sdSSID;
            _password = sdPassword;
            if (sdHostname.length() > 0)
                _hostname = sdHostname;
            
            if (_pConfigBase)
            {
                _pConfigBase->setConfigData(formConfigStr().c_str());
                _pConfigBase->writeConfig();
            }
            
            // If portal was already started due to no credentials, stop it
            if (_portalMode)
            {
                Log.notice("%sStopping portal mode to try SD card credentials\n", MODULE_PREFIX);
                stopPortalMode();
            }
            
            // Reset connection state and force a connection attempt
            _connectionFailures = 0;
            _connectionAttemptStartTime = 0;
            _lastWifiBeginAttemptMs = 0;  // Force immediate connection attempt
            _wifiFirstBeginDone = false;
            
            Log.notice("%sWiFi credentials loaded from SD card, will attempt connection...\n", MODULE_PREFIX);
            return true;
        }
        else
        {
            Log.warning("%sSD card .network file has wifi=yes but missing WiFiSSID\n", MODULE_PREFIX);
        }
    }
    else if (wifiMode == "ap")
    {
        Log.notice("%sSD card .network file specifies AP mode\n", MODULE_PREFIX);
        // Clear any existing credentials to force AP mode
        clearCredentials();
        return true;
    }
    else
    {
        Log.trace("%sSD card .network file wifi mode '%s' not recognized\n", MODULE_PREFIX, wifiMode.c_str());
    }
    
    return false;
}

String WiFiManager::getPortalHTML()
{
    return R"HTMLDELIM(<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>sandBot WiFi Setup</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }
        .container { max-width: 400px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h1 { color: #333; text-align: center; margin-bottom: 30px; }
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; font-weight: bold; }
        input[type="text"], input[type="password"], select { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }
        button { width: 100%; padding: 10px; background: #007cba; color: white; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }
        button:hover { background: #005f8a; }
        .status { margin-top: 15px; padding: 10px; border-radius: 4px; text-align: center; }
        .success { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
        .error { background: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
        .network-list { margin-bottom: 15px; }
        .network-item { padding: 8px; margin: 5px 0; background: #f8f9fa; border: 1px solid #dee2e6; border-radius: 4px; cursor: pointer; }
        .network-item:hover { background: #e9ecef; }
        .signal-strength { float: right; }
    </style>
</head>
<body>
    <div class="container">
        <h1>sandBot WiFi Setup</h1>
        <div style="background: #e3f2fd; padding: 10px; border-radius: 4px; margin-bottom: 20px; font-size: 14px;">
            Connect to your WiFi network. Click "Scan Networks" to see available options or enter network details manually.
        </div>
        <button type="button" id="scanButton" onclick="scanNetworks()" style="width: 100%; padding: 8px; margin-bottom: 15px; background: #28a745; color: white; border: none; border-radius: 4px; cursor: pointer;">Scan Networks</button>
        <div id="networkList" class="network-list"></div>
        <form id="wifiForm">
            <div class="form-group">
                <label for="ssid">Network Name (SSID):</label>
                <input type="text" id="ssid" name="ssid" required>
            </div>
            <div class="form-group">
                <label for="password">Password:</label>
                <input type="password" id="password" name="password">
            </div>
            <div class="form-group">
                <label for="hostname">Device Hostname (optional):</label>
                <input type="text" id="hostname" name="hostname" placeholder="Leave empty to keep current">
            </div>
            <button type="submit">Connect</button>
        </form>
        <div id="status"></div>
    </div>

    <script>
        // Scan for WiFi networks
        function scanNetworks() {
            const networkList = document.getElementById('networkList');
            const scanButton = document.getElementById('scanButton');
            
            // Disable button during scan
            scanButton.disabled = true;
            scanButton.innerHTML = 'Scanning...';
            scanButton.style.background = '#6c757d';
            
            networkList.innerHTML = '<h3>Scanning for networks...</h3>';
            
            function performScan(isFirstCall) {
                // On first call, add parameter to start new scan
                const url = isFirstCall ? '/wifiscan?start=1' : '/wifiscan';
                fetch(url)
                    .then(response => response.json())
                    .then(data => {
                        if (data.scanning) {
                            // Scan still in progress, retry in 500ms without start parameter
                            setTimeout(() => performScan(false), 500);
                            return;
                        }
                        
                        // Re-enable scan button
                        function enableScanButton() {
                            scanButton.disabled = false;
                            scanButton.innerHTML = 'Scan Networks';
                            scanButton.style.background = '#28a745';
                        }
                        
                        if (data.error) {
                            networkList.innerHTML = '<h3>Error scanning networks</h3>';
                            enableScanButton();
                            return;
                        }
                        
                        // Display results
                        networkList.innerHTML = '<h3>Available Networks:</h3>';
                        if (data.networks.length === 0) {
                            networkList.innerHTML += '<div style=\"padding: 10px; color: #666;\">No networks found</div>';
                        } else {
                            data.networks.forEach(network => {
                                const div = document.createElement('div');
                                div.className = 'network-item';
                                div.innerHTML = '<span>' + network.ssid + '</span><span class=\"signal-strength\">' + network.rssi + 'dBm</span>';
                                div.onclick = () => {
                                    document.getElementById('ssid').value = network.ssid;
                                };
                                networkList.appendChild(div);
                            });
                        }
                        
                        enableScanButton();
                    })
                    .catch(error => {
                        console.error('Error scanning networks:', error);
                        networkList.innerHTML = '<h3>Error scanning networks</h3>';
                        // Re-enable scan button on error
                        scanButton.disabled = false;
                        scanButton.innerHTML = 'Scan Networks';
                        scanButton.style.background = '#28a745';
                    });
            }
            
            performScan(true); // Start with first call parameter
        }

        // Handle form submission
        document.getElementById('wifiForm').addEventListener('submit', function(e) {
            e.preventDefault();
            const ssid = document.getElementById('ssid').value;
            const password = document.getElementById('password').value;
            const hostname = document.getElementById('hostname').value;
            
            const statusDiv = document.getElementById('status');
            statusDiv.innerHTML = '<div class=\"status\">Connecting...</div>';
            
            // Submit WiFi credentials
            fetch('/w/' + encodeURIComponent(ssid) + '/' + encodeURIComponent(password) + '/' + encodeURIComponent(hostname))
                .then(response => response.text())
                .then(data => {
                    statusDiv.innerHTML = '<div class=\"status success\">WiFi credentials saved! Device will restart and connect to the network.</div>';
                    setTimeout(() => {
                        window.location.reload();
                    }, 3000);
                })
                .catch(error => {
                    statusDiv.innerHTML = '<div class=\"status error\">Failed to save WiFi credentials. Please try again.</div>';
                });
        });

        // No automatic scanning - user must click "Scan Networks" button
    </script>
</body>
</html>)HTMLDELIM";
}

