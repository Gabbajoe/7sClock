# 7sClock — ESP8266 7-Segment LED Smart Clock

A smart WiFi-connected clock using 7-segment style LED displays, powered by the ESP8266. It includes a full-featured mobile-friendly web interface for configuration and OTA updates.

## ✨ Features

- ⏰ **Time Sync**: Syncs time over NTP with automatic DST via configurable timezone (e.g., Europe/Berlin)
- 🌐 **WiFiManager**: Easy setup via captive portal
- 🌈 **Web UI**: Fully featured configuration portal
  - LED color and brightness
  - Blink dots / solid dots
  - 12h / 24h time format (with optional leading zero)
  - Auto-dimming at night
  - NTP sync interval
  - Time zone (selectable from dropdown)
- 📱 **Responsive Design**: Mobile-friendly UI
- 🔧 **Persistent Config**: Saves settings to flash
- 🔁 **OTA**: Firmware updates via web interface
- 🧠 **MDNS**: Access your clock via `http://7sclock.local`

## 🔧 Hardware

- ESP8266 D1 Mini (or similar)
- 2x 7-segment LED modules driven by NeoPixel-compatible WS2812 LEDs
- Power supply (5V)

## 🛠️ Setup with PlatformIO

**platformio.ini**
```ini
[env:d1_mini]
platform = espressif8266
board = d1_mini
framework = arduino
monitor_speed = 115200
upload_speed = 921600
build_flags = 
  -D PIO_FRAMEWORK_ARDUINO_LWIP2_HIGHER_BANDWIDTH
  -D LED_BUILTIN=2
  -D PIO_FRAMEWORK_ARDUINO_LITTLEFS_ENABLE
  -D ARDUINOJSON_USE_DOUBLE=0
lib_compat_mode = strict
lib_deps =
  bblanchon/ArduinoJson
  me-no-dev/ESPAsyncWebServer
  me-no-dev/ESPAsyncTCP
  adafruit/Adafruit NeoPixel
  alanswx/ESPAsyncWiFiManager
  ArduinoOTA
upload_port = 7sclock.local
```

## 🔌 Web Interface

Access the clock at:
http://7sclock.local (or via IP from serial log)

Configure everything from a single page:
- Choose timezone (e.g. Europe/Berlin)
- Set sync interval (e.g. every 3600s)
- Preview and pick LED colors
- Enable/disable blinking dots
- Reboot or update OTA firmware

## 📲 OTA Updates

Upload firmware via the web interface:

Navigate to http://7sclock.local
Choose firmware .bin file
Wait for upload and auto-reboot

## 🕓 Timezone Note

This project uses [POSIX timezone strings](https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html), not IANA zone names like `"Europe/Berlin"`.

When configuring the timezone, use the **POSIX TZ string** format that includes DST rules.

### 🌍 Common Timezone Mapping Table

| Region          | IANA Zone        | POSIX TZ String                                | Notes                                |
|-----------------|------------------|-------------------------------------------------|--------------------------------------|
| Germany         | Europe/Berlin    | `CET-1CEST,M3.5.0/2,M10.5.0/3`                 | CET/CEST with DST                   |
| UK              | Europe/London    | `GMT0BST,M3.5.0/1,M10.5.0/2`                   | GMT/BST with DST                    |
| USA East Coast  | America/New_York | `EST5EDT,M3.2.0/2,M11.1.0/2`                   | Eastern Time                        |
| USA West Coast  | America/Los_Angeles | `PST8PDT,M3.2.0/2,M11.1.0/2`               | Pacific Time                        |
| Japan           | Asia/Tokyo       | `JST-9`                                        | No DST                              |
| China           | Asia/Shanghai    | `CST-8`                                        | No DST                              |
| Australia (SYD) | Australia/Sydney | `AEST-10AEDT,M10.1.0,M4.1.0/3`                 | Sydney w/ DST                       |

For a full list of POSIX TZ rules, see the [ESP8266 TZ format reference](https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv).

## 🚀 Build & Flash

```bash
git clone https://github.com/yourname/7sclock.git
cd 7sclock
platformio run --target upload
```
