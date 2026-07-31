# Indoor Weather Station

A connected indoor weather station built on the ESP32-C3 Super Mini that displays real-time temperature, altitude, humidity, and a live weather forecast icon on a 1.8" TFT screen.

---

## What it does

On boot the device:
1. Initializes the BMP280 sensor over I2C and takes a baseline pressure reading.
2. Draws a static four-panel UI on the TFT display (labels, borders).
3. Connects to Wi-Fi.

Every main loop iteration:
1. Reads **temperature** and **altitude** locally from the BMP280.
2. Refreshes the four display panels with the latest values.
3. Makes an HTTP GET request to the **OpenWeatherMap (OWM)** current-weather endpoint to retrieve:
   - Outdoor **humidity** (the BMP280 has no humidity sensor).
   - **Sea-level pressure** (used to compute accurate altitude via the barometric formula).
   - A **weather icon code** (e.g. `10d`) that selects the matching bitmap.
4. Waits **30 minutes** before repeating (or **30 seconds** if Wi-Fi is not yet connected, to keep retrying).

---

## Hardware components

| Component | Details |
|---|---|
| Microcontroller | ESP32-C3 Super Mini |
| Display | 1.8" 128×160 SPI TFT, ST7735 controller (black-tab variant) |
| Sensor | BMP280 — temperature + barometric pressure (I2C at address `0x76`) |
| Backlight | TFT LED controlled via PWM (dimmed to duty cycle 32/255) |

### Pin wiring

| Signal | GPIO |
|---|---|
| TFT CS | 7 |
| TFT RST | 5 |
| TFT DC | 10 |
| TFT LED (backlight PWM) | 21 |
| BMP280 SDA | 0 |
| BMP280 SCL | 1 |

---

## Libraries required

Install these via the Arduino Library Manager or manually:

- **Adafruit ST7735 and ST7789 Library** — TFT driver
- **Adafruit GFX Library** — graphics primitives and font support
- **Adafruit BMP280 Library** — sensor driver
- **ArduinoJson** — JSON parsing for the OWM response
- `SPI`, `Wire`, `WiFi`, `HTTPClient` — bundled with the ESP32 Arduino core

---

## Configuration

Before flashing, set your credentials at the top of `Indoor_Weather.ino`:

```cpp
const char* SSID    = "your_ssid";
const char* PASS    = "your_password";
const char* OWM_KEY = "openweather_api_key";
const char* CITY_ID = "openweather_city_id";   // numeric city ID, not name
```

Get a free API key at [openweathermap.org](https://openweathermap.org) and find your city's numeric ID via the OWM website or API.

---

## Display layout

The display runs in **landscape mode** (160×128 px). The screen is divided into four fixed panels, each with a rounded-rectangle border and a Spanish label:

```
┌─────────────────────────────────┐
│  P1 TIEMPO   │   P2 TEMP        │
│  (icon 48×48)│   22.3 °C        │
├──────────────┼──────────────────┤
│  P3 ALTITUD  │   P4 HUMEDAD     │
│  2240 mts    │   65 %           │
└─────────────────────────────────┘
```

| Panel | Position | Content |
|---|---|---|
| P1 — TIEMPO | Top-left | OWM weather icon (48×48 px bitmap, centered) |
| P2 — TEMP | Top-right | Local temperature in °C (FreeSans9pt7b + degree circle) |
| P3 — ALTITUD | Bottom-left | Altitude in metres, computed from sensor pressure and OWM sea-level pressure |
| P4 — HUMEDAD | Bottom-right | Outdoor relative humidity (%) from OWM |

Panel borders and labels are drawn once in `drawStaticUI()`. Each loop cycle only erases and repaints the value area inside each panel (a `fillRect` the exact size of the data region), avoiding a full-screen refresh and eliminating visible flicker.

---

## Weather icons

OWM returns a short icon code with each weather response (e.g. `01d` = clear sky day, `10n` = rain night). The sketch ships with all standard OWM icon codes pre-baked as **48×48 monochrome bitmaps** stored in flash (`PROGMEM`) as C byte arrays.

The bitmaps were sourced from the [OWM weather conditions page](https://openweathermap.org/api/weather-conditions) and converted to C arrays using the [LCD Assistant tool](https://projedefteri.com/en/tools/lcd-assistant/).

`drawWeatherIcon()` does a linear scan of the `weatherIcons[]` lookup table to match the code string to its bitmap, then calls `tft.drawBitmap()` with the icon centered inside panel P1. Unknown codes fall back to `i01d` (clear sky day).

Night variants that share the same silhouette as their day counterparts (e.g. `03n`, `04n`, `09n`, `11n`, `13n`, `50n`) reuse the day bitmap to save flash space.

---

## Architectural decisions

### Why Wi-Fi is required
The BMP280 measures temperature and barometric pressure only — it has no humidity sensor. Humidity is a key comfort metric, so the sketch fetches it from OWM over HTTP. This also pulls the sea-level pressure needed for accurate altitude calculation and the current weather icon for the forecast panel.

### Partial panel repainting
Instead of calling `fillScreen()` on every loop (which causes a full white flash), the sketch erases only the data region inside each panel with a `fillRect` in the background colour. The static chrome (borders, labels) is never touched after the initial draw.

### Dual-font rendering
Each panel shows its numeric value in `FreeSans9pt7b` (larger, proportional) and its unit suffix in the built-in 5×7 pixel font (smaller). The code switches fonts mid-panel so the units read visually subordinate to the value without needing a second draw pass.

### BMP280 sampling configuration
The sensor is configured following the datasheet recommendation for weather monitoring:
- Temperature oversampling ×2, pressure oversampling ×16 (pressure is the primary measurement)
- IIR filter coefficient ×16 (smooths short-term fluctuations from door/window disturbances)
- Normal mode with 500 ms standby between internal measurements

### Backlight PWM dimming
The TFT backlight LED is driven through a LEDC PWM channel at 5 kHz, 8-bit resolution, duty cycle 32 (≈12.5%). This reduces eye strain and power consumption compared to driving the backlight at full brightness.

### Refresh cadence
When Wi-Fi is connected, the loop sleeps for 30 minutes between OWM fetches — matching the typical rate at which local indoor conditions change meaningfully and staying well within OWM's free-tier rate limits. When Wi-Fi is disconnected the sleep drops to 30 seconds so the device recovers quickly once a connection becomes available.
