/*
  NonBlocking - Event-driven Speech-to-Text with callbacks & background tasks

  Ideal for projects where your ESP32 has other tasks to perform:
    - Driving OLED/TFT displays, NeoPixels, animations, or sensor loops
    - Serving web pages, handling MQTT, or managing BLE connections
    - Live audio VU level meter visualization

  Key Architecture:
    - stt.prewarm() keeps the TLS connection hot while idle for ZERO-delay starts.
    - stt.tick() does bounded, quick processing per iteration - never blocking loop().
    - Callbacks notify your sketch immediately when transcripts arrive or errors occur.
    - Can be triggered by a button, touch pin, Serial command, MQTT, or timers!

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
// 2. Hardware Pin Configuration (Customizable for ANY board / pins)
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

// Trigger button pin (active LOW with internal pull-up):
// Set to 0 for onboard BOOT, or any GPIO. Set to -1 if using software/Serial triggers!
const int PIN_BUTTON  = 0;

// Initialize GroqSTT instance
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

// =============================================================================
// 3. Non-Blocking Event Callbacks
// =============================================================================

// Fired when transcription completes successfully
void onTranscript(const String& text) {
  Serial.println("\n---------------------------------------------");
  Serial.print("[EVENT] Transcript: \"");
  Serial.print(text);
  Serial.println("\"");
  Serial.printf("[EVENT] Latency   : %.0f ms (Upload: %.0f ms | Inference: %.0f ms)\n",
                stt.lastLatencyMs(), stt.lastSendMs(), stt.lastInferMs());
  Serial.println("---------------------------------------------\n");
}

// Fired if an error occurs (network drop, silence, rate limit, etc.)
void onError(groq_stt_err_t err, const String& detail) {
  Serial.println();
  Serial.print("[EVENT ERROR] ");
  Serial.print(groq_stt_errorText(err));
  Serial.print(": ");
  Serial.print(detail);
  Serial.printf(" (HTTP %u)\n\n", stt.lastHttpStatus());
}

// Fired on every audio block with current RMS amplitude (0.0 to 1.0)
// Use this to drive OLED VU meters, RGB LEDs, or volume indicators!
void onAudioLevel(float rms01) {
  static uint32_t lastMeter = 0;
  if (millis() - lastMeter > 100) {
    lastMeter = millis();
    int bars = (int)(rms01 * 30.0f);
    if (bars > 20) bars = 20;

    Serial.print("\r[Mic Level] [");
    for (int i = 0; i < 20; i++) Serial.print(i < bars ? "#" : " ");
    Serial.print("]");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=============================================");
  Serial.println("          GroqSTT - NonBlocking              ");
  Serial.println("=============================================");

  connectWiFi();

  // Register our callbacks
  stt.onDone(onTranscript);
  stt.onError(onError);
  stt.onLevel(onAudioLevel);

  // ===========================================================================
  // 4. Complete Library Configuration & Tuning
  // ===========================================================================

  // --- Model Selection ---
  // Options: "whisper-large-v3-turbo" (Fastest, default),
  //          "whisper-large-v3" (Maximum accuracy for accents),
  //          "distil-whisper-large-v3-en" (English only)
  stt.setModel("whisper-large-v3-turbo");

  // Language: ISO-639-1 code ("en", "es", "de", "hi", etc.) or "" for auto-detect
  stt.setLanguage("en");

  // Audio gain (1 - 16, default 8) and high-pass filter (default 120 Hz)
  stt.setGain(8);
  stt.setHighpassHz(120);

  // Minimum peak threshold to prevent hallucinations on silence (default 300)
  stt.setSilenceThreshold(300);

  // Initialize library
  if (!stt.begin(GROQ_API_KEY)) {
    Serial.print("[INIT FAIL] ");
    Serial.println(stt.errorText());
    while (true) delay(1000);
  }

  Serial.println("[INIT OK] Ready!");
  Serial.println(">> Triggers: Press BOOT button OR type 'r' in Serial to record <<\n");
}

void loop() {
  // ---------------------------------------------------------------------------
  // 1. Keep TLS Connection Pre-Warmed While Idle
  // Pre-warms the TLS socket to api.groq.com so triggers start INSTANTLY!
  // ---------------------------------------------------------------------------
  static uint32_t lastPrewarm = 0;
  if (!stt.isBusy() && millis() - lastPrewarm > 2000) {
    lastPrewarm = millis();
    stt.prewarm(); // maintains hot TLS socket in background
  }

  // ---------------------------------------------------------------------------
  // 2. Hardware Button Trigger (Push & Hold, release to stop)
  // ---------------------------------------------------------------------------
  if (PIN_BUTTON >= 0 && !stt.isBusy() && digitalRead(PIN_BUTTON) == LOW) {
    delay(30); // debounce
    if (digitalRead(PIN_BUTTON) == LOW) {
      stt.startRecording();
      Serial.println("\n[REC] Recording started (via button)... release to transcribe!");
    }
  }

  // ---------------------------------------------------------------------------
  // 3. Software Trigger Example (e.g. Serial command, MQTT, touch pin, etc.)
  // Type 'r' in Serial to toggle recording on/off!
  // ---------------------------------------------------------------------------
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'R') {
      if (!stt.isRecording() && !stt.isBusy()) {
        stt.startRecording();
        Serial.println("\n[REC] Recording started (via Serial command)... Type 'r' again to stop!");
      } else if (stt.isRecording()) {
        stt.stopRecording();
        Serial.println("\n[REC] Stopping recording and uploading...");
      }
    }
  }

  // ---------------------------------------------------------------------------
  // 4. Drive the State Machine
  // tick() advances recording, chunk streaming, and reply parsing.
  // Never blocks your loop!
  // ---------------------------------------------------------------------------
  stt.tick();

  // ---------------------------------------------------------------------------
  // 5. Your Other Project Tasks Go Here! (Displays, NeoPixels, sensors, etc.)
  // ---------------------------------------------------------------------------
  static uint32_t heartbeat = 0;
  if (!stt.isRecording() && millis() - heartbeat > 2000) {
    heartbeat = millis();
    // Your background tasks run seamlessly here
  }

  delay(1);
}
