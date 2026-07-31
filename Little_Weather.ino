// Indoor weather station
// Graphic UI and WIFI Version
//
// platform: ESP32-C3 Super Mini
// display: 128*160 SPI 1.8 inch diagonal TFT ST7735 controller
// Sensor: BMP280

// As the BMP280 DOES NOT measure humidity locally, we need to fetch it from
// the interwebs :(
// This constraint makes it a "connected" device :_(

#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_ST7735.h>
#include <Fonts/FreeSans9pt7b.h>
#include <weather_icons.h>

const char* SSID = "your_ssid";
const char* PASS = "your_wifi_pass";
const char* OWM_KEY = "your_openweather_api_key";
const char* CITY_ID = "your_openweather_city_id";

// Pin definitions
#define TFT_CS 7
#define TFT_RST 5
#define TFT_DC 10
#define TFT_LED 21
#define BM_SDA 0
#define BM_SCL 1

Adafruit_BMP280 bmp;
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// Panel Coordinates & Dimensions (X, Y, Width, Height)
// calculated for 160x128 Landscape display
#define P1_X 3  // Top-Left (Forecast)
#define P1_Y 3
#define P1_W 76
#define P1_H 59

#define P2_X 82  // Top-Right (Temp)
#define P2_Y 3
#define P2_W 76
#define P2_H 59

#define P3_X 3  // Bottom-Left (Pressure)
#define P3_Y 65
#define P3_W 76
#define P3_H 60

#define P4_X 82  // Bottom-Right (Humidity)
#define P4_Y 65
#define P4_W 76
#define P4_H 60

// Display Colors (RGB565 format)
#define LINE_DARK tft.color565(30, 30, 80)
#define COLOR_BONE tft.color565(227, 218, 201)

// Variables for forecasting
float currentPressure = 0.0;
float humidity = 0.0;
float seaLevelPressure = 0.0;
String currentIconCode = "01d";


// Weather icons as bitmaps
// extracted from openweathermap (https://openweathermap.org/api/weather-conditions)
// and converted to C-array bitmap so we can store it as bytecode, for efficiency
// (https://image2cpp.com/)
struct IconMap {
	const char* name;
	const uint16_t* bitmap;
};

const IconMap weatherIcons[] = {
	{ "01d", i01d },
	{ "01n", i01n },
	{ "02d", i02d },
	{ "02n", i02n },
	{ "03d", i03d },
	{ "03n", i03d },
	{ "04d", i04d },
	{ "04n", i04d },
	{ "09d", i09d },
	{ "09n", i09d },
	{ "10d", i10d },
	{ "10n", i10n },
	{ "11d", i11d },
	{ "11n", i11d },
	{ "13d", i13d },
	{ "13n", i13d },
	{ "50d", i50d },
	{ "50n", i50d },
};
const uint8_t iconCount = sizeof(weatherIcons) / sizeof(weatherIcons[0]);

void drawWeatherIcon(int16_t x, int16_t y, String code) {
	const uint16_t* bitmapToDraw = nullptr;


	// Scan to match icon code with layout definition (bitmap above)
	for (uint8_t i = 0; i < iconCount; i++) {
		if (code == weatherIcons[i].name) {
			bitmapToDraw = weatherIcons[i].bitmap;
			break;
		}
	}

	if (bitmapToDraw == nullptr) {
		bitmapToDraw = i01d;  // Default fallback icon
	}

	// Centering math inside P1 Panel (P1_W = 84, P1_H = 59, Icon = 48x48)
	int16_t iconX = P1_X + ((P1_W - 48) / 2);  // Center horizontally
	int16_t iconY = P1_Y + ((P1_H - 36) / 2);  // Center vertically

	// Render on screen
	//tft.drawRGBBitmap(iconX, iconY, bitmapToDraw, 48, 48);

	// as drawRGBBitmap doesn't support icons with transparent background
	// we're gonna need to draw the icon pixel by pixel and skip the 'transparecy' value a.k.a 0x0000
	tft.startWrite();
	const uint16_t transparentKey = 0x0000;
	int w = 48;
	int h = 48;

  for (int16_t j = 0; j < h; j++) {
    for (int16_t i = 0; i < w; i++) {
      uint16_t color = pgm_read_word(&bitmapToDraw[j * w + i]);
      if (color != transparentKey) {
        tft.writePixel(iconX + i, iconY + j, color);
      }
    }
  }
  tft.endWrite();
}

void drawStaticUI() {
	tft.fillScreen(COLOR_BONE);

	// Draw the 4 panels
	tft.drawRoundRect(P1_X, P1_Y, P1_W, P1_H, 4, LINE_DARK);
	tft.drawRoundRect(P2_X, P2_Y, P2_W, P2_H, 4, LINE_DARK);
	tft.drawRoundRect(P3_X, P3_Y, P3_W, P3_H, 4, LINE_DARK);
	tft.drawRoundRect(P4_X, P4_Y, P4_W, P4_H, 4, LINE_DARK);

	// Draw standard titles using the default 5x7 font
	tft.setFont();
	tft.setTextColor(LINE_DARK);
	tft.setTextSize(1);

	tft.setCursor(P1_X + 23, P1_Y + 5);
	tft.print("TIEMPO");
	tft.setCursor(P2_X + 5, P2_Y + 5);
	tft.print("TEMPERATURA");
	tft.setCursor(P3_X + 18, P3_Y + 5);
	tft.print("ALTITUD");
	tft.setCursor(P4_X + 20, P4_Y + 5);
	tft.print("HUMEDAD");

	drawWeatherIcon(20, 30, currentIconCode);
}

void fetchOutdoor() {
	// Fetch weather data from openweathermap as we cannot source forecast
	// and humidity locally
	HTTPClient http;
	String url = String("http://api.openweathermap.org/data/2.5/weather?id=") + CITY_ID + "&units=metric&appid=" + OWM_KEY;
	http.begin(url);
	if (http.GET() == 200) {
		StaticJsonDocument<1024> d;
		deserializeJson(d, http.getString());
		humidity = d["main"]["humidity"].as<float>();
		seaLevelPressure = d["main"]["sea_level"].as<float>();
		currentIconCode = d["weather"][0]["icon"].as<String>();
	}
	http.end();
}

void setup() {
	Serial.begin(115200);
	Wire.begin(BM_SDA, BM_SCL);

	ledcAttach(TFT_LED, 5000, 8);  // Setup a PWM channel (pin_number, frequency, resolution)
	ledcWrite(TFT_LED, 153);        // Set the PWM output for brightness on TFT backlight (pin_number, duty_cycle) out of 255

	tft.initR(INITR_BLACKTAB);
	tft.setRotation(1);

	bmp.begin(0x76);

	// Default settings from datasheet for standard weather monitoring
	bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,      // Operating Mode
	                Adafruit_BMP280::SAMPLING_X2,      // Temp. oversampling
	                Adafruit_BMP280::SAMPLING_X16,     // Pressure oversampling
	                Adafruit_BMP280::FILTER_X16,       // Filtering
	                Adafruit_BMP280::STANDBY_MS_500);  // Standby time

	// Take initial pressure reading
	currentPressure = bmp.readPressure() / 100.0F;  // Convert Pa to hPa

	drawStaticUI();

	WiFi.begin(SSID, PASS);
	delay(2500);  // allow 5 secs for connection
}

void loop() {
	float temp = bmp.readTemperature();
	float alt = bmp.readAltitude(seaLevelPressure);

	// Erase the old values by painting blue boxes inside the panels
	tft.fillRect(P1_X + 2, P1_Y + 16, P1_W - 4, P1_H - 18, COLOR_BONE);
	tft.fillRect(P2_X + 2, P2_Y + 16, P2_W - 4, P2_H - 18, COLOR_BONE);
	tft.fillRect(P3_X + 2, P3_Y + 16, P3_W - 4, P3_H - 18, COLOR_BONE);
	tft.fillRect(P4_X + 2, P4_Y + 16, P4_W - 4, P4_H - 18, COLOR_BONE);

	// Setup the larger font for the numeric values
	tft.setFont(&FreeSans9pt7b);
	tft.setTextColor(LINE_DARK);
	tft.setTextSize(1);

	// Print weather icon
	drawWeatherIcon(20, 30, currentIconCode);

	// Print Temperature
	tft.setCursor(P2_X + 18, P2_Y + 45);
	tft.printf("%.1f", temp);
	tft.setFont();                                       // Switch back to default font for units to make them smaller
	tft.drawCircle(P2_X + 50, P2_Y + 28, 2, LINE_DARK);  // Degree symbol
	tft.setCursor(P2_X + 56, P2_Y + 24);
	tft.print("C");

	// Print Altitude
	tft.setFont(&FreeSans9pt7b);
	tft.setCursor(P3_X + 12, P3_Y + 45);
	tft.printf("%.0f", alt);
	tft.setFont();
	tft.setCursor(P3_X + 50, P3_Y + 36);
	tft.print("mts");

	// Print Humidity
	tft.setFont(&FreeSans9pt7b);
	tft.setCursor(P4_X + 22, P4_Y + 45);
	tft.printf("%.0f", humidity);
	tft.setFont();
	tft.setCursor(P4_X + 46, P4_Y + 36);
	tft.print("%");

	if (WiFi.status() == WL_CONNECTED) {
		Serial.println("Wifi Connected");
		delay(1800000);  // Wait 30 minutes
	} else {
		Serial.println("Wifi NOT Connected");
		delay(30000);  // Wait 30 seconds
	}
	fetchOutdoor();
}
