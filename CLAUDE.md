# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-based e-paper weather display that fetches data from OpenWeatherMap API and displays it on a 4.2" EPD (400x300 resolution). The project uses deep sleep for power efficiency and supports multiple language localizations.

## Build & Upload

This is an Arduino project. Use Arduino IDE or PlatformIO:

```bash
# Arduino CLI compile
arduino-cli compile --fqbn esp32:esp32:esp32s3 main.ino

# Arduino CLI upload
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32s3 main.ino
```

## Architecture

### File Structure
- `main.ino` - Main sketch with display rendering, WiFi, and sleep logic
- `common.h` - Weather data structures (`Forecast_record_type`, `WxConditions`, `WxForecast`), API calls, JSON decoding
- `owm_credentials.h` - WiFi credentials, OWM API key, location config (LAT/LON), timezone settings
- `lang.h` - English UI strings and localization constants
- `epaper_fonts.h` - U8g2 font definitions (large file)

### Key Data Flow
1. `setup()` connects WiFi, syncs time via NTP
2. `ReceiveOneCallWeather()` fetches from OWM API 3.0 `/data/3.0/onecall`
3. `DecodeOneCallWeather()` parses JSON into `WxConditions[0]` (current), `WxForecast[]` (hourly), `Daily[]` (7-day)
4. `DisplayWeather()` renders all sections to the e-paper
5. `BeginSleep()` enters deep sleep for `SleepDuration` minutes (default: 1 min)

### Hardware Configuration
Currently configured for CrowPanel ESP32-S3 with GDEY042T81 display. Pin mappings in `main.ino:43-50`:
- EPD_BUSY=48, EPD_CS=45, EPD_RST=47, EPD_DC=46, EPD_SCK=12, EPD_MOSI=11

Alternative pin configs for LOLIN D32 and Waveshare boards are commented out.

### Display Sections
- `DrawHeadingSection()` - City, date, time, battery
- `DrawMainWeatherSection()` - Wind compass, current temp/humidity, weather icon, description
- `DrawForecastSection()` - 3 forecast boxes + pressure/temp/rain graphs
- `DrawAstronomySection()` - Sunrise/sunset, moon phase

## Configuration

Edit `owm_credentials.h`:
- `ssid`, `password` - WiFi credentials
- `apikey` - OpenWeatherMap API key (requires 3.0 subscription)
- `LAT`, `LON` - Location coordinates
- `City`, `Country`, `Language`, `Hemisphere`
- `Units` - "M" (metric) or "I" (imperial)
- `Timezone`, `gmtOffset_sec`, `daylightOffset_sec` - Time zone config

## Dependencies

- GxEPD2 - E-paper display driver
- U8g2_for_Adafruit_GFX - Font rendering
- ArduinoJson - JSON parsing
- WiFi, HTTPClient, SPI (built-in)

## Site Data API (SITE_API.md)

Separate REST API for querying IoT site data from DynamoDB (not part of the ESP32 display code):
- Deploy: `./deploy_api.sh deploy`
- Test: `./test_api.sh`
- Endpoint: `GET /site?site_name=<name>&count=<n>`
