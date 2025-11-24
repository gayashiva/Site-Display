# Site Display - ESP-IDF Version

ESP32-S3 e-paper display for IoT site sensor data, converted from Arduino to ESP-IDF framework.

## Hardware

- **MCU**: ESP32-S3 with OPI PSRAM (CrowPanel)
- **Display**: GDEY042T81 4.2" B/W e-paper (400x300)
- **Flash**: 8MB
- **Input**: Rotary switch (UP/DOWN) + Fetch button

### Pin Configuration (CrowPanel ESP32-S3)

| Function | GPIO |
|----------|------|
| EPD Power | 7 |
| EPD Busy | 48 |
| EPD Reset | 47 |
| EPD DC | 46 |
| EPD CS | 45 |
| EPD SCK | 12 |
| EPD MOSI | 11 |
| Rotary UP | 6 |
| Rotary DOWN | 4 |
| Fetch Button | 2 |

## Build Instructions

### Prerequisites

1. Install ESP-IDF v5.x: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/

2. Set up environment:
   ```bash
   . $HOME/esp/esp-idf/export.sh
   ```

### Configure

```bash
cd esp-idf
idf.py set-target esp32s3
idf.py menuconfig
```

Configure under "Site Display Configuration":
- WiFi credentials
- Site Data API settings
- NTP/Timezone settings
- GPIO pins (if different from defaults)

### Build

```bash
idf.py build
```

### Flash

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Or with specific baud rate:
```bash
idf.py -p /dev/ttyUSB0 -b 921600 flash monitor
```

### Monitor Only

```bash
idf.py -p /dev/ttyUSB0 monitor
```

Press `Ctrl+]` to exit monitor.

## Project Structure

```
esp-idf/
├── CMakeLists.txt              # Root project config
├── sdkconfig.defaults          # Default SDK settings (ESP32-S3, PSRAM)
├── main/
│   ├── CMakeLists.txt          # Main component config
│   ├── Kconfig.projbuild       # Menuconfig options
│   ├── main.c                  # Entry point (app_main)
│   ├── display.c/h             # High-level display functions
│   ├── wifi_manager.c/h        # WiFi connection handling
│   ├── http_client.c/h         # HTTPS API client
│   ├── site_data.c/h           # Data structures & JSON parsing
│   ├── nvs_storage.c/h         # Site selection persistence
│   └── lang.h                  # UI strings
└── components/
    └── epaper/                 # E-paper driver component
        ├── CMakeLists.txt
        ├── epaper.c            # SPI driver for GDEY042T81
        ├── epaper_fonts.c      # Bitmap fonts
        └── include/
            └── epaper.h
```

## Usage

1. Power on the device
2. Use rotary switch to select site (UP/DOWN)
3. Press fetch button to retrieve and display data
4. Display shows:
   - Site name and location
   - Current temperature (air & water)
   - Pressure reading
   - 6-hour history graphs

## API Endpoint

The device fetches data from:
```
GET https://<server>/prod/site?site_name=<name>&count=<n>
```

Response format:
```json
{
  "site_name": "Sakti",
  "site_type": "air",
  "active": true,
  "current": {
    "dt": 1234567890,
    "temperature": 5.2,
    "water_temp": 2.1,
    "pressure": 0.85,
    "voltage": 3.7,
    "counter": 123
  },
  "readings": [...]
}
```

## Key Differences from Arduino Version

| Feature | Arduino | ESP-IDF |
|---------|---------|---------|
| WiFi | `WiFi.h` | `esp_wifi.h` + event-driven |
| HTTP | `HTTPClient.h` | `esp_http_client.h` |
| JSON | ArduinoJson | cJSON (built-in) |
| Storage | Preferences | NVS Flash |
| Strings | String class | char arrays |
| Main loop | `loop()` | FreeRTOS tasks |
| GPIO | digitalRead/Write | gpio_get/set_level |
| Delays | delay() | vTaskDelay() |

## Troubleshooting

### WiFi Connection Issues
- Check SSID/password in menuconfig
- Verify router is 2.4GHz (ESP32 doesn't support 5GHz)

### Display Not Updating
- Check SPI pin connections
- Verify EPD power pin is toggling
- Check for BUSY pin timeout in logs

### HTTPS Certificate Errors
- ESP-IDF uses certificate bundle by default
- Update IDF if certificates are outdated

### PSRAM Issues
- Ensure `CONFIG_SPIRAM_MODE_OCT=y` is set
- Check PSRAM chip is properly soldered
