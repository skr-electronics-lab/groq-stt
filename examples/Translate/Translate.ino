/*
  Translate - Speak in ANY language -> Get translated English text + timestamps

  Demonstrates real-time speech translation powered by Groq Whisper:
    - Automatically detects spoken language (Spanish, French, Hindi, Japanese, etc.)
    - Translates spoken words directly into English on Groq Cloud
    - Requests verbose JSON output with per-segment start/end timestamps
    - Parses JSON output using ArduinoJson

  Hardware Wiring (INMP441 I2S Microphone):
    - VDD -> 3.3V (NOT 5V)
    - GND -> GND
    - L/R -> GND (Left channel)
    - SD  -> Data In  (Default: GPIO 22 on classic ESP32, GPIO 4 on ESP32-S3)
    - SCK -> BCLK     (Default: GPIO 26 on classic ESP32, GPIO 5 on ESP32-S3)
    - WS  -> LRCLK    (Default: GPIO 25 on classic ESP32, GPIO 6 on ESP32-S3)

  Get a free Groq API key at: https://console.groq.com/keys
*/

#include <Arduino.h>
#include <WiFi.h>
#include <groq_stt.h>
#include <ArduinoJson.h>

// =============================================================================
// 1. Wi-Fi & Groq API Credentials
// =============================================================================
const char* WIFI_SSID    = "YOUR_WIFI";
const char* WIFI_PASS    = "YOUR_PASSWORD";
const char* GROQ_API_KEY = "gsk_...";

// =============================================================================
// 2. Hardware Pin Configuration (Change for your board / wiring)
// =============================================================================
#if defined(CONFIG_IDF_TARGET_ESP32S3)
const int PIN_MIC_SCK = 5;
const int PIN_MIC_WS  = 6;
const int PIN_MIC_SD  = 4;
#else
const int PIN_MIC_SCK = 26;
const int PIN_MIC_WS  = 25;
const int PIN_MIC_SD  = 22;
#endif

const int PIN_BUTTON  = 0; // Onboard BOOT button, or any GPIO connected to GND

GroqSTT stt(PIN_MIC_SCK, PIN_MIC_WS, PIN_MIC_SD, PIN_BUTTON);

// --- Wi-Fi Connection Helper -------------------------------------------------
static void connectWiFi() {
  Serial.print("[NET] Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("[NET] Connected! IP: ");
  Serial.println(WiFi.localIP());
}

// --- Parse and display verbose JSON with timestamps -------------------------
static void printTranslation(const String& jsonBody) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, jsonBody);
  if (err) {
    Serial.print("[JSON ERROR] Failed to parse: ");
    Serial.println(err.c_str());
    return;
  }

  const char* detectedLang = doc["language"] | "unknown";
  const char* englishText  = doc["text"] | "";

  Serial.println("\n=============================================");
  Serial.printf("Detected Language : %s\n", detectedLang);
  Serial.printf("English Transcript: \"%s\"\n", englishText);
  Serial.println("---------------------------------------------");
  Serial.println("Segment Timestamps:");

  for (JsonVariantConst s : doc["segments"].as<JsonArrayConst>()) {
    float startSec = s["start"] | 0.0f;
    float endSec   = s["end"]   | 0.0f;
    const char* txt = s["text"]  | "";
    Serial.printf("  [%5.2fs -> %5.2fs] %s\n", startSec, endSec, txt);
  }
  Serial.println("=============================================\n");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=============================================");
  Serial.println("            GroqSTT - Translate              ");
  Serial.println("=============================================");

  connectWiFi();

  // ===========================================================================
  // 3. Library Configuration & Tuning
  // ===========================================================================

  // --- Translation Mode ---
  // Routes to Groq's /audio/translations endpoint (auto-selects whisper-large-v3).
  stt.setTranslate(true);

  // Spoken Language: "" means auto-detect whatever language you speak!
  stt.setLanguage("");

  // Response Format: STT_FMT_VERBOSE_JSON provides segment timestamps.
  stt.setResponseFormat(STT_FMT_VERBOSE_JSON);

  // Audio Tuning:
  // Cuts frequencies below 120 Hz to strip DC offset and desk/fan rumble,
  // freeing headroom so digital gain (8) amplifies your voice cleanly.
  stt.setGain(8);
  stt.setHighpassHz(120);
  stt.setSilenceThreshold(300);

  // Temperature: 0.0 (strict) to 1.0 (creative). 0.2 gives consistent translations.
  stt.setTemperature(0.2f);

  // Context Prompt (Optional): Biases translation toward specific technical terms:
  // stt.setPrompt("ESP32, microcontroller, robotics, IoT");

  // Hands-free VAD Tuning (Optional - when PIN_BUTTON = -1):
  // stt.setButtonPin(-1);        // Disable button for hands-free
  // stt.useVad(true);            // Auto-stop when you stop talking
  // stt.setVadSilenceMs(1200);   // Trailing silence pause before upload (ms)
  // stt.setVadSpeechRatio(2.0f); // Voice sensitivity (1.5 = quiet room, 2.5+ = noisy)

  if (!stt.begin(GROQ_API_KEY)) {
    Serial.print("[INIT FAIL] ");
    Serial.println(stt.errorText());
    while (true) delay(1000);
  }

  Serial.println("[INIT OK] Ready!");
  Serial.println(">> Hold BOOT, speak in ANY language (Spanish, Hindi, French...), release <<\n");
}

void loop() {
  String response = stt.listen();

  if (response.length() > 0) {
    printTranslation(response);
    Serial.printf("Latency: %.0f ms (Upload: %.0f ms | Inference: %.0f ms)\n\n",
                  stt.lastLatencyMs(), stt.lastSendMs(), stt.lastInferMs());
  } else if (stt.lastError() != STT_OK) {
    Serial.print("[ERROR] ");
    Serial.print(stt.errorText());
    Serial.printf(" (HTTP %u)\n\n", stt.lastHttpStatus());
  }
}
