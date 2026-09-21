/*
  VoiceLed - Control hardware GPIOs by voice: "Turn on the light" / "Turn off the LED"

  Demonstrates voice command parsing and physical device control:
    - Transcribes natural speech in real-time
    - Context prompt hints guide Whisper to recognize command keywords accurately
    - Parses commands ("on", "off", "toggle") and controls an LED / relay / GPIO
    - Works with Push-to-Talk or hands-free Voice Activity Detection (VAD)

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

// =============================================================================
// 1. Wi-Fi & Groq API Credentials
// =============================================================================
const char* WIFI_SSID    = "YOUR_WIFI";
const char* WIFI_PASS    = "YOUR_PASSWORD";
const char* GROQ_API_KEY = "gsk_...";

// =============================================================================
// 2. Hardware Pin Configuration (Customizable for ANY board)
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

#ifdef LED_BUILTIN
const int PIN_LED = LED_BUILTIN;
#else
const int PIN_LED = 2;     // Classic ESP32 onboard blue LED
#endif

GroqSTT stt(PIN_MIC_SCK, PIN_MIC_WS, PIN_MIC_SD, PIN_BUTTON);
bool ledState = false;

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

static void setLed(bool on) {
  ledState = on;
  digitalWrite(PIN_LED, ledState ? HIGH : LOW);
  Serial.printf("[LED] Now %s\n", ledState ? "ON (HIGH)" : "OFF (LOW)");
}

// --- Parse spoken sentence into actions --------------------------------------
static void processVoiceCommand(const String& rawText) {
  String s = rawText;
  s.toLowerCase();

  Serial.println("\n---------------------------------------------");
  Serial.printf("You Said: \"%s\"\n", rawText.c_str());

  if (s.indexOf("turn on") >= 0 || s.indexOf("switch on") >= 0 || s.indexOf("light on") >= 0) {
    setLed(true);
  } else if (s.indexOf("turn off") >= 0 || s.indexOf("switch off") >= 0 || s.indexOf("light off") >= 0) {
    setLed(false);
  } else if (s.indexOf("toggle") >= 0) {
    setLed(!ledState);
  } else {
    Serial.println("[ACTION] Command not recognized. Try saying:");
    Serial.println("         - \"Turn on the LED\"");
    Serial.println("         - \"Turn off the LED\"");
    Serial.println("         - \"Toggle the light\"");
  }
  Serial.println("---------------------------------------------\n");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=============================================");
  Serial.println("             GroqSTT - VoiceLed              ");
  Serial.println("=============================================");

  pinMode(PIN_LED, OUTPUT);
  setLed(false);

  connectWiFi();

  // ===========================================================================
  // 3. Complete Library Configuration & Tuning
  // ===========================================================================

  // Model: "whisper-large-v3-turbo" gives lightning-fast command response (< 400ms)
  stt.setModel("whisper-large-v3-turbo");

  stt.setLanguage("en");

  // Prompt Hint: Feeds Whisper keyword context to maximize recognition accuracy
  // of short command phrases:
  stt.setPrompt("turn on, turn off, toggle, LED, light");

  // Audio gain & filters
  stt.setGain(8);
  stt.setHighpassHz(120);
  stt.setSilenceThreshold(300);

  if (!stt.begin(GROQ_API_KEY)) {
    Serial.print("[INIT FAIL] ");
    Serial.println(stt.errorText());
    while (true) delay(1000);
  }

  Serial.println("[INIT OK] Ready!");
  Serial.println(">> Hold BOOT, say \"Turn on the light\" or \"Turn off the LED\", release <<\n");
}

void loop() {
  String text = stt.listen();

  if (text.length() > 0) {
    processVoiceCommand(text);
  } else if (stt.lastError() != STT_OK) {
    Serial.print("[ERROR] ");
    Serial.print(stt.errorText());
    Serial.printf(" (HTTP %u)\n\n", stt.lastHttpStatus());
  }
}
