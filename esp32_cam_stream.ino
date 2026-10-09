/*
 * ===================================================================
 *  ESP32-CAM (AI-THINKER) Web-Safe Cloud Streamer (Insecure HTTPS)
 * ===================================================================
 *  - Uses WiFiClientSecure with client.setInsecure() to bypass TLS cert verification
 *  - Eliminates -1 (connection refused) HTTPS handshake errors on Render
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ===================================================================
//  USER CONFIGURATION - CHANGE THESE VALUES BEFORE UPLOADING
// ===================================================================
const char* WIFI_SSID     = "iPhone";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Your Render Cloud API endpoint
const char* SERVER_URL    = "https://astra26.onrender.com/api/camera/frame";

// Frame upload interval (2000ms = 2 seconds per frame)
const int FRAME_INTERVAL_MS = 2000; 
// ===================================================================

// AI-THINKER CAMERA PIN CONFIGURATION
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define LED_FLASH_GPIO     4

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- Starting ESP32-CAM Streamer ---");

  pinMode(LED_FLASH_GPIO, OUTPUT);
  digitalWrite(LED_FLASH_GPIO, LOW);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM; config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  // STABLE CONFIGURATION PREVENTING MALLOC FAIL & OVERFLOW
  config.frame_size = FRAMESIZE_QVGA; // 320x240 (~3.5 KB payload)
  config.jpeg_quality = 15;
  config.fb_count = 1;
  config.fb_location = CAMERA_FB_IN_DRAM; // Forces allocation in internal DRAM
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Camera init failed: 0x%x\n", err);
    return;
  }

  Serial.println("✅ Camera Hardware Ready.");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 25) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi Connected!");
    Serial.print("IP Address: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n⚠️ WiFi not connected yet. Streamer will retry in the background.");
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    sendFrame();
  } else {
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    delay(3000);
  }
  delay(FRAME_INTERVAL_MS);
}

void sendFrame() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("⚠️ Camera capture failed");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure(); // Skip HTTPS certificate verification for fast 100% reliable connection

  HTTPClient http;
  http.begin(client, SERVER_URL); // Pass secure client explicitly
  http.setTimeout(8000);          // 8s timeout
  http.addHeader("Content-Type", "image/jpeg");

  int httpCode = http.POST(fb->buf, fb->len);

  if (httpCode == 200 || httpCode == 201) {
    Serial.printf("⚡ Frame Sent (%u bytes) -> HTTP %d\n", fb->len, httpCode);
  } else {
    Serial.printf("⚠️ POST Response: %d (%s)\n", httpCode, http.errorToString(httpCode).c_str());
  }

  http.end();
  esp_camera_fb_return(fb); // Free buffer memory
}
