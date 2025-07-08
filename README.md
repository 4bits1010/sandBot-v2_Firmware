# WiFi Portal Enhancement & SD Card Configuration

## Overview
This update adds a comprehensive WiFi portal system and SD card-based network configuration to the SandBot firmware, providing multiple methods for WiFi setup and eliminating the need for serial terminal configuration in most scenarios.

While this code can work on other ESP microcontrollers like the adafruit Huzzah32, I have updated this repo to favor the [Adafruit feather v2](https://learn.adafruit.com/adafruit-esp32-feather-v2/overview) board as it is the newest variant with some handy features I preferred. Consider upgrading or starting with this board for a drop-in compile as well as new features coming soon. 

## 🚀 New Features

### 1. **WiFi Portal Mode**
A captive portal system that automatically activates when WiFi configuration is needed.

**When Portal Activates:**
- No WiFi credentials stored in device
- WiFi connection fails after 3 attempts (wrong password, network unavailable, etc.)
- Manual activation via `/wc` (clear credentials) command

**Portal Features:**
- **Network Name:** `sandBot-XXXXXX` (where XXXXXX = last 6 digits of MAC address)
- **Password Protected:**
- **Captive Portal:** Automatic browser redirect on connection
- **Network Scanner:** Lists available WiFi networks with signal strength
- **Click-to-Select:** Tap networks to auto-fill SSID
- **Real-time Feedback:** Connection status and error reporting

### 2. **SD Card Network Configuration**
Automatic WiFi setup from configuration file on SD card.

**File Location:** `/sd/.network` (root of SD card)

**Configuration Examples:**

#### Connect to WiFi Network:
```json
{
  "wifi": "yes",
  "WiFiSSID": "MyHomeNetwork", 
  "WiFiPW": "MyPassword123",
  "WiFiHostname": "sandBot-workshop"
}
```

#### Force Portal Mode:
```json
{
  "wifi": "ap"
}
```

#### Open Network (no password):
```json
{
  "wifi": "yes",
  "WiFiSSID": "PublicWiFi",
  "WiFiPW": ""
}
```

**Behavior:**
- Checked on every boot after SD card mount
- Overrides stored NVS credentials
- Falls back to portal mode if SD credentials fail
- Supports hostname customization

### 3. **Enhanced Connection Failure Detection**
Improved WiFi failure handling with multiple trigger conditions.

**Portal Mode Triggers:**
- ✅ **No Credentials** - Immediate portal activation
- ✅ **Connection Timeout** - 30 seconds without connection
- ✅ **Authentication Failure** - Wrong password detection
- ✅ **Network Not Found** - SSID unavailable
- ✅ **Connection Failures** - Any disconnection during connection attempt
- ✅ **Failure Threshold** - 3 consecutive failures → portal mode

### 4. **Motor Timeout Fix**
Fixed stepper motor disable functionality.  This was not functioning, now after idle the motor enable is disabled so the steppers are turned off and put into a powersave mode to also prevent excess heat buildup.

**Issue:** Motors were not disabling after idle timeout  
**Fix:** Added missing `_motorEnabler.service()` call in motion control loop  
**Result:** Motors now properly disable after 10 seconds of inactivity  

## 🔧 Technical Changes

### WiFiManager Enhancements

**New Methods:**
- `loadNetworkConfigFromSD()` - SD card configuration parser
- `isPortalMode()` - Portal state checking
- `startPortalMode()` / `stopPortalMode()` - Portal lifecycle management
- `getPortalHTML()` - Web interface generation

**New Properties:**
- Portal timeout control (disabled by default)
- Connection failure tracking
- DNS server for captive portal
- Dynamic AP SSID generation

### REST API Updates

**New Endpoints:**
- `/wifiscan` - Returns available networks as JSON
- `/wifiportal` - Serves portal configuration page

**Fixed Endpoints:**
- `/wc` - WiFi clear now properly activates portal mode
- Content-Type headers now respect endpoint specifications

### FileManager Integration

**New Method:**
- `isSDCardOk()` - Public SD card status checking

## 📱 User Experience

### Portal Access Flow
1. **Connect to WiFi** → `sandBot-XXXXXX`
2. **Browser Opens** → Captive portal loads automatically
3. **Select Network** → Click from scanned list or type manually
4. **Enter Password** → WiFi credentials
5. **Submit** → Device connects and exits portal mode

### SD Card Setup Flow
1. **Create File** → `/sd/.network` with JSON configuration
2. **Insert SD Card** → Into device
3. **Power On** → Automatic configuration on boot
4. **Connection** → Immediate WiFi connection or fallback to portal

- Quickly switch between networks via portal interface

**Field Service:**
- Portal activates automatically on WiFi failures
- Technicians can reconfigure without serial access

## 🔍 Debugging & Monitoring

### Log Messages

**SD Card Configuration:**
```
I: WiFiManager: Found .network file on SD card, parsing...
I: WiFiManager: SD card WiFi config: SSID=MyNetwork, hostname=MyHostname
I: WiFiManager: WiFi credentials loaded from SD card, will attempt connection...
```

**Portal Activation:**
```
I: WiFiManager: Max connection failures (3) reached, starting portal mode
I: WiFiManager: Starting WiFi portal mode with SSID: sandBot-A1B2C3
I: WiFiManager: WiFi Portal started - Connect to sandBot-A1B2C3 (password: ********) and visit http://192.168.4.1
```

**Motor Timeout:**
```
I: MotorEnabler: enabled, disable after idle 10.00s
I: MotorEnabler: timeout motors disabled by timeout
```

### Troubleshooting

**Portal Not Appearing:**
- Check for existing valid WiFi credentials in NVS
- Use `/wc` command to clear and force portal mode
- Verify 3 connection failures occurred

**SD Configuration Not Working:**
- Verify SD card mounting: Look for "SD details" in logs
- Check file location: Must be `/sd/.network` (note the dot)
- Validate JSON syntax: Use online JSON validator
- Check file permissions and encoding (UTF-8)

**Connection Issues:**
- Monitor failure count in logs
- Verify network availability and password
- Check signal strength and network security type

## 🛡️ Security

**Portal Access:** Password-protected setup network prevents unauthorized access  
**Timeout Protection:** Portal auto-disables after inactivity (configurable)  
**Credential Storage:** NVS encryption maintains WiFi password security  

## 🔄 Backward Compatibility

**Existing Commands:** All legacy serial commands remain functional  
**NVS Storage:** Existing WiFi credentials preserved and honored  
**Configuration:** Previous robot configurations unchanged  
**API Endpoints:** All existing REST endpoints maintain compatibility  

## 📋 Configuration Reference

NOTE: change the softAP password (apPassword=) in WiFiManager.cpp

### SD Card .network File Fields

| Field | Required | Type | Description |
|-------|----------|------|-------------|
| `wifi` | ✅ | String | `"yes"` for WiFi mode, `"ap"` for portal mode |
| `WiFiSSID` | ⚠️* | String | Target network name (*required if wifi="yes") |
| `WiFiPW` | ❌ | String | Network password (empty for open networks) |
| `WiFiHostname` | ❌ | String | Custom device hostname |

### Portal Configuration

| Setting | Value | Description |
|---------|-------|-------------|
| **AP SSID** | `sandBot-XXXXXX` | X = last 6 MAC digits |
| **AP Password** | NOTE:CHANGEIT | Secure setup access |
| **AP IP** | `192.168.4.1` | Default ESP32 AP address |
| **Portal Timeout** | Disabled | Remains active until configured |
| **Failure Threshold** | 3 attempts | Before portal activation |
| **Connection Timeout** | 30 seconds | Per attempt before failure |

This update significantly improves the WiFi setup experience while maintaining full backward compatibility with existing deployments.

---

## Original Project Info

RBotFirmware
============

Firmware for an ESP32 or Particle device to control robots of various geometries
More information here https://robdobson.com/2017/02/a-line-in-the-sand/
Forked from https://github.com/robdobsn/RBotFirmware
