/*
 * ===================================================================
 *  ESP32 Multi-Sensor Cloud Telemetry System
 *  Sensors: DS18B20 Temp + LDR Light + Voltage + Rain + Tilt
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
 *     - DO (Digital Out)--> GPIO 33 (Optional)
 * 
 *  5. Tilt / Vibration Switch Sensor:
 *     - VCC           --> 3.3V
 *     - GND           --> GND
 *     - DO (Digital Out)--> GPIO 25 (INPUT_PULLUP)
 * 
 *  REQUIRED LIBRARIES:
 *  1. OneWire (by Jim Studt, Paul Stoffregen, etc.)
 *  2. DallasTemperature (by Miles Burton)
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ===================================================================
//  USER CONFIGURATION - CHANGE THESE VALUES BEFORE UPLOADING
// ===================================================================
const char* WIFI_SSID     = "YOUR_WIFI_SSID";         // Your Wi-Fi Name
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";     // Your Wi-Fi Password

// Your deployed Render server URL
const char* SERVER_URL    = "https://astra26.onrender.com/api/temperature";

// Optional API key if configured on server (leave empty "" if not using)
const char* API_KEY       = ""; 

// Telemetry reading send interval (in milliseconds)
const unsigned long SEND_INTERVAL_MS = 5000; // 5 seconds

// Pin Definitions
#define ONE_WIRE_BUS      4   // DS18B20 Temp Sensor Pin
#define LDR_PIN           34  // LDR Light Sensor Analog Pin
#define VOLTAGE_PIN       35  // Voltage Divider Sensor Analog Pin
#define RAIN_PIN          32  // Rain Sensor Analog Pin
#define TILT_PIN          25  // Tilt / Motion Switch Digital Pin
#define STATUS_LED        2   // Onboard Status LED

// Voltage Calibration Constants (Adjust for your resistor divider ratio)
const float VOLTAGE_DIVIDER_FACTOR = 5.0; // Standard 5:1 Voltage Divider module (0-25V)
const float ESP32_ADC_REF_VOLTS   = 3.3; // ESP32 ADC reference voltage
const int   ADC_RESOLUTION       = 4095;
// ===================================================================

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

unsigned long lastSendTime = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  pinMode(TILT_PIN, INPUT_PULLUP);
  analogReadResolution(12); // 12-bit ADC (0 - 4095)

  Serial.println("\n-------------------------------------------");
  Serial.println(" ESP32 Multi-Sensor Telemetry System");
  Serial.println(" DS18B20 + LDR + Voltage + Rain + Tilt");
  Serial.println("-------------------------------------------");

  // Initialize temperature sensor
  sensors.begin();
  int deviceCount = sensors.getDeviceCount();
  Serial.print("Found ");
  Serial.print(deviceCount);
  Serial.println(" DS18B20 sensor(s) on OneWire bus.");

  // Connect to Wi-Fi
  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  unsigned long currentMillis = millis();
  if (currentMillis - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = currentMillis;
    sendTelemetryData();
  }
}

void connectWiFi() {
  Serial.print("Connecting to WiFi network: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 30) {
    delay(500);
    Serial.print(".");
    digitalWrite(STATUS_LED, !digitalRead(STATUS_LED)); // Blink while connecting
    attempt++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(STATUS_LED, HIGH); // Solid ON when connected
    Serial.println("\n✅ WiFi Connected Successfully!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    digitalWrite(STATUS_LED, LOW);
    Serial.println("\n❌ WiFi Connection Failed! Will retry in next loop cycle.");
  }
}

void sendTelemetryData() {
  // 1. Read DS18B20 Waterproof Temperature Sensor
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);
  if (tempC == DEVICE_DISCONNECTED_C || tempC < -55.0 || tempC > 125.0) {
    tempC = 25.0; // Fallback default if disconnected
  }
  float tempF = (tempC * 9.0 / 5.0) + 32.0;

  // 2. Read LDR Light Sensor (0 - 4095 ADC)
  int rawLdr = analogRead(LDR_PIN);
  // Convert 0-4095 to Light Percentage (0% = Dark, 100% = Bright)
  float ldrPercent = ((4095 - rawLdr) / 4095.0) * 100.0;
  if (ldrPercent < 0) ldrPercent = 0;
  if (ldrPercent > 100) ldrPercent = 100;

  // 3. Read Voltage Sensor (0 - 25V Divider)
  int rawVolts = analogRead(VOLTAGE_PIN);
  float pinVolts = (rawVolts / (float)ADC_RESOLUTION) * ESP32_ADC_REF_VOLTS;
  float measuredVoltage = pinVolts * VOLTAGE_DIVIDER_FACTOR;

  // 4. Read Rain Sensor (0 = Wet/Raining, 4095 = Dry)
  int rawRain = analogRead(RAIN_PIN);
  float rainPercent = ((4095 - rawRain) / 4095.0) * 100.0;
  if (rainPercent < 0) rainPercent = 0;
  if (rainPercent > 100) rainPercent = 100;
  bool rainDetected = (rainPercent > 15.0);

  // 5. Read Tilt Sensor (0 = Tilted, 1 = Stable)
  int tiltState = digitalRead(TILT_PIN);
  bool tiltDetected = (tiltState == LOW);

  int rssi = WiFi.RSSI();

  Serial.printf("📊 Readouts -> Temp: %.2f°C | Light: %.1f%% | Voltage: %.2fV | Rain: %.1f%% | Tilt: %s\n",
                tempC, ldrPercent, measuredVoltage, rainPercent, tiltDetected ? "TILTED!" : "STABLE");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Skipping HTTP POST.");
    return;
  }

  HTTPClient http;
  if (String(SERVER_URL).startsWith("https")) {
    WiFiClientSecure client;
    client.setInsecure(); // Skip certificate verification for HTTPS targets like Render
    http.begin(client, SERVER_URL);
  } else {
    http.begin(SERVER_URL);
  }

  http.addHeader("Content-Type", "application/json");
  if (strlen(API_KEY) > 0) {
    http.addHeader("x-api-key", API_KEY);
  }

  // Construct JSON Payload
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

  // Double-blink status LED
  digitalWrite(STATUS_LED, LOW); delay(40);
  digitalWrite(STATUS_LED, HIGH); delay(40);
  digitalWrite(STATUS_LED, LOW); delay(40);
  digitalWrite(STATUS_LED, HIGH);

  int httpResponseCode = http.POST(jsonPayload);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.printf("✅ POST Response %d: %s\n", httpResponseCode, response.c_str());
  } else {
    Serial.printf("❌ POST Failed: %d (%s)\n", httpResponseCode, http.errorToString(httpResponseCode).c_str());
  }

  http.end();
}
