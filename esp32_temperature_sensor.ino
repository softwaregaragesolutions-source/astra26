/*
 * ===================================================================
 *  ESP32 Multi-Sensor Cloud Telemetry System
 *  Sensors: DS18B20 Temp + LDR Light + Voltage + Rain + Tilt
 *  Target Cloud: https://astra26.onrender.com/api/temperature
 * ===================================================================
 * 
 *  WIRING SCHEMATIC:
 *  -----------------
 *  1. DS18B20 Waterproof Temp Sensor:
 *     - VCC (Red)     --> 3.3V or 5V
 *     - GND (Black)   --> GND
 *     - Data (Yellow) --> GPIO 4 (Add 4.7kΩ resistor between VCC & Data)
 * 
 *  2. LDR Light Sensor Module:
 *     - VCC           --> 3.3V
 *     - GND           --> GND
 *     - AO (Analog Out)--> GPIO 34 (ADC1_CH6)
 * 
 *  3. Voltage Sensor Module (0-25V Range):
 *     - VCC / S (Signal)--> GPIO 35 (ADC1_CH7)
 *     - GND           --> GND
 * 
 *  4. Raindrop Sensor Module:
 *     - VCC           --> 3.3V / 5V
 *     - GND           --> GND
 *     - AO (Analog Out)--> GPIO 32 (ADC1_CH4)
 * 
 *  5. Tilt / Vibration Switch Sensor:
 *     - VCC           --> 3.3V
 *     - GND           --> GND
 *     - DO (Digital Out)--> GPIO 25 (INPUT_PULLUP)
 * 
 *  REQUIRED LIBRARIES (Install via Arduino Library Manager):
 *  1. OneWire (by Paul Stoffregen)
 *  2. DallasTemperature (by Miles Burton)
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "esp_log.h"

// ===================================================================
//  USER CONFIGURATION - VERIFY BEFORE UPLOADING
// ===================================================================
// Set your Wi-Fi or Personal Hotspot credentials:
const char* WIFI_SSID     = "iPhone";                 // Your Wi-Fi / Hotspot SSID
const char* WIFI_PASSWORD = "00000000";               // Your iPhone Hotspot Password

// Render Cloud API Endpoint
const char* SERVER_URL    = "https://astra26.onrender.com/api/temperature";

// Optional API key (leave empty "" unless configured on server)
const char* API_KEY       = ""; 

// Telemetry reading interval (in milliseconds)
const unsigned long SEND_INTERVAL_MS = 5000; // 5 seconds

// Pin Definitions
#define ONE_WIRE_BUS      4   // DS18B20 OneWire Pin
#define LDR_PIN           34  // LDR Light Sensor Analog Pin (ADC1_CH6)
#define VOLTAGE_PIN       35  // Voltage Divider Sensor Analog Pin (ADC1_CH7)
#define RAIN_PIN          32  // Rain Sensor Analog Pin (ADC1_CH4)
#define TILT_PIN          25  // Tilt Switch Digital Pin
#define STATUS_LED        -1  // Disabled (-1) to avoid GPIO 2 conflict. Set to 13, 26, etc. if using external LED.

// Voltage Calibration Constants (Standard 5:1 Resistor Divider: 0 - 25V)
const float VOLTAGE_DIVIDER_FACTOR = 5.0;
const float ESP32_ADC_REF_VOLTS   = 3.3;
const int   ADC_RESOLUTION       = 4095;
// ===================================================================

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

unsigned long lastSendTime = 0;
unsigned long lastWiFiRetryTime = 0;
const unsigned long WIFI_RETRY_INTERVAL_MS = 10000; // Retry Wi-Fi every 10s if disconnected

void setup() {
  // 1. Suppress internal PHY antenna warning logs in ESP-IDF
  esp_log_level_set("phy_comm", ESP_LOG_NONE);
  esp_log_level_set("wifi", ESP_LOG_WARN);

  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // Prevents modem sleep; critical for iPhone/mobile hotspots

  if (STATUS_LED >= 0) {
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);
  }

  pinMode(TILT_PIN, INPUT_PULLUP);
  analogReadResolution(12); // 12-bit ADC (0 - 4095)

  Serial.println("\n===========================================");
  Serial.println("  ESP32 Multi-Sensor Telemetry System");
  Serial.println("  Sensors: DS18B20 + LDR + Voltage + Rain + Tilt");
  Serial.println("===========================================");

  // Initialize DS18B20
  sensors.begin();
  int deviceCount = sensors.getDeviceCount();
  Serial.print("🌡️ Found ");
  Serial.print(deviceCount);
  Serial.println(" DS18B20 sensor(s) on OneWire bus.");

  // Initial Wi-Fi connection attempt
  connectWiFiInitial();
}

void loop() {
  unsigned long currentMillis = millis();

  // Background Wi-Fi self-healing
  if (WiFi.status() != WL_CONNECTED) {
    if (currentMillis - lastWiFiRetryTime >= WIFI_RETRY_INTERVAL_MS) {
      lastWiFiRetryTime = currentMillis;
      reconnectWiFi();
    }
  }

  // Periodic sensor read & telemetry transmit
  if (currentMillis - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = currentMillis;
    processAndSendTelemetry();
  }
}

#include <lwip/dns.h>

// Initial Wi-Fi attempt during setup (with diagnostics)
void connectWiFiInitial() {
  Serial.print("\n📡 Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  // Quick 2.4GHz pre-scan to check hotspot visibility
  Serial.print("🔍 Scanning 2.4GHz channels for '");
  Serial.print(WIFI_SSID);
  Serial.println("'...");

  int n = WiFi.scanNetworks();
  bool ssidFound = false;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == WIFI_SSID) {
      ssidFound = true;
      Serial.printf("   ✅ Detected '%s' (Signal: %d dBm, Channel: %d)\n", WIFI_SSID, WiFi.RSSI(i), WiFi.channel(i));
      break;
    }
  }

  if (!ssidFound) {
    Serial.printf("   ⚠️ '%s' NOT visible in 2.4GHz!\n", WIFI_SSID);
    Serial.println("   👉 iPhone: Settings -> Personal Hotspot -> Turn ON 'Maximize Compatibility'");
    Serial.println("   👉 Keep the 'Personal Hotspot' screen OPEN and unlocked on your phone.");
  }

  Serial.print("Connecting");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 25) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    // Configure robust Public DNS (Google 8.8.8.8 & Cloudflare 1.1.1.1) to avoid iPhone DNS dropouts
    ip_addr_t d1, d2;
    IP_ADDR4(&d1, 8, 8, 8, 8);
    IP_ADDR4(&d2, 1, 1, 1, 1);
    dns_setserver(0, &d1);
    dns_setserver(1, &d2);

    if (STATUS_LED >= 0) digitalWrite(STATUS_LED, HIGH);
    Serial.println("\n✅ WiFi Connected Successfully!");
    Serial.print("   IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.printf("   Signal Strength: %d dBm\n", WiFi.RSSI());
  } else {
    if (STATUS_LED >= 0) digitalWrite(STATUS_LED, LOW);
    Serial.println("\n⚠️ WiFi not connected yet. Telemetry will continue reading sensors");
    Serial.println("   and will automatically reconnect in the background.\n");
  }
}

// Non-blocking background Wi-Fi reconnect
void reconnectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("🔄 Background reconnecting to '%s'...\n", WIFI_SSID);
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// Read all sensors, print to Serial Monitor, and POST to Render Cloud
void processAndSendTelemetry() {
  // 1. Read DS18B20 Waterproof Temperature Sensor
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);
  if (tempC == DEVICE_DISCONNECTED_C || tempC < -55.0 || tempC > 125.0) {
    tempC = 25.0; // Fallback default if probe is disconnected or floating
  }
  float tempF = (tempC * 9.0 / 5.0) + 32.0;

  // 2. Read LDR Light Sensor (0 - 4095 ADC)
  int rawLdr = analogRead(LDR_PIN);
  float ldrPercent = ((4095 - rawLdr) / 4095.0) * 100.0;
  if (ldrPercent < 0) ldrPercent = 0;
  if (ldrPercent > 100) ldrPercent = 100;

  // 3. Read Voltage Sensor (0 - 25V Divider)
  int rawVolts = analogRead(VOLTAGE_PIN);
  float pinVolts = (rawVolts / (float)ADC_RESOLUTION) * ESP32_ADC_REF_VOLTS;
  float measuredVoltage = pinVolts * VOLTAGE_DIVIDER_FACTOR;

  // 4. Read Rain Sensor (0 = Submerged, 4095 = Completely Dry)
  int rawRain = analogRead(RAIN_PIN);
  float rainPercent = ((4095 - rawRain) / 4095.0) * 100.0;
  if (rainPercent < 0) rainPercent = 0;
  if (rainPercent > 100) rainPercent = 100;
  bool rainDetected = (rainPercent > 15.0);

  // 5. Read Tilt Sensor (0 = Tilted/Triggered, 1 = Stable)
  int tiltState = digitalRead(TILT_PIN);
  bool tiltDetected = (tiltState == LOW);

  int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;

  // Print live sensor readings to Serial Monitor immediately
  Serial.printf("📊 [TELEMETRY] Temp: %.2f°C (%.1f°F) | Light: %.1f%% | Volts: %.2fV | Rain: %.1f%% | Tilt: %s | WiFi: %s\n",
                tempC, tempF, ldrPercent, measuredVoltage, rainPercent, 
                tiltDetected ? "TILTED!" : "STABLE",
                (WiFi.status() == WL_CONNECTED) ? "ONLINE" : "OFFLINE");

  // If Wi-Fi is not connected, skip network POST
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("   ⏳ Waiting for WiFi connection to upload to Render...");
    return;
  }

  // Pre-check DNS resolution
  IPAddress hostIP;
  if (!WiFi.hostByName("astra26.onrender.com", hostIP)) {
    Serial.println("   ❌ DNS Error: Cannot resolve astra26.onrender.com.");
    Serial.println("   👉 Check iPhone: Make sure Cellular / Mobile Data is turned ON!");
    return;
  }

  // Construct JSON Payload for Render Webhook
  String jsonPayload = "{";
  jsonPayload += "\"temperature\":" + String(tempC, 2) + ",";
  jsonPayload += "\"temp_f\":" + String(tempF, 2) + ",";
  jsonPayload += "\"ldr_percent\":" + String(ldrPercent, 1) + ",";
  jsonPayload += "\"voltage\":" + String(measuredVoltage, 2) + ",";
  jsonPayload += "\"rain_percent\":" + String(rainPercent, 1) + ",";
  jsonPayload += "\"rain_detected\":" + String(rainDetected ? "true" : "false") + ",";
  jsonPayload += "\"tilt_detected\":" + String(tiltDetected ? "true" : "false") + ",";
  jsonPayload += "\"sensor_id\":\"ESP32_MULTI_SENSOR\",";
  jsonPayload += "\"rssi\":" + String(rssi);
  jsonPayload += "}";

  WiFiClientSecure client;
  client.setInsecure(); // Skip certificate verification for smooth HTTPS handshake with Render
  client.setHandshakeTimeout(30);

  HTTPClient http;
  http.begin(client, SERVER_URL);
  http.setReuse(false);
  http.setTimeout(12000); // 12-second timeout for mobile networks
  http.setUserAgent("Mozilla/5.0 (ESP32)");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Host", "astra26.onrender.com");
  http.addHeader("Connection", "close");

  if (strlen(API_KEY) > 0) {
    http.addHeader("x-api-key", API_KEY);
  }

  // Double-blink status LED if configured
  if (STATUS_LED >= 0) {
    digitalWrite(STATUS_LED, LOW); delay(30);
    digitalWrite(STATUS_LED, HIGH); delay(30);
    digitalWrite(STATUS_LED, LOW); delay(30);
    digitalWrite(STATUS_LED, HIGH);
  }

  int httpResponseCode = http.POST(jsonPayload);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.printf("   ☁️ Render POST Success [%d]: %s\n", httpResponseCode, response.c_str());
  } else {
    Serial.printf("   ❌ Render POST Failed: %d (%s)\n", httpResponseCode, http.errorToString(httpResponseCode).c_str());
    char lastErr[128] = {0};
    client.lastError(lastErr, sizeof(lastErr));
    if (strlen(lastErr) > 0) {
      Serial.printf("   👉 TLS Socket Diagnostic: %s\n", lastErr);
    }
  }

  http.end();
  client.stop();
}
