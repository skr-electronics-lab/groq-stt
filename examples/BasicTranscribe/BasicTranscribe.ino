/*
  BasicTranscribe - The complete, flexible starter sketch for GroqSTT

  Universal Speech-to-Text for ESP32 & ESP32-S3:
    - Hold a button and talk (Push-to-Talk)
    - OR run hands-free with VAD (Voice Activity Detection, no buttons needed!)
    - Streams audio directly from I2S mic (INMP441/SPH0645) to Groq Whisper over TLS
    - Zero SD card, zero PSRAM, and zero audio buffer RAM required

  Hardware Wiring (INMP441 I2S Microphone):
    - VDD -> 3.3V (NOT 5V! 3.3V only)
    - GND -> GND
    - L/R -> GND (selects Left channel)
    - SD  -> Data In  (Default: GPIO 22 on classic ESP32, GPIO 4 on ESP32-S3)
    - SCK -> BCLK     (Default: GPIO 26 on classic ESP32, GPIO 5 on ESP32-S3)
    - WS  -> LRCLK    (Default: GPIO 25 on classic ESP32, GPIO 6 on ESP32-S3)

  Push-to-Talk Button:
    - Onboard BOOT button is on GPIO 0 (active LOW, internal pull-up enabled).
    - Or wire any momentary tactile button between ANY GPIO and GND.
    - Or set PIN_BUTTON = -1 and enable VAD for hands-free operation!

  Get a free Groq API key at: https://console.groq.com/keys
*/

#include <Arduino.h>
#include <WiFi.h>
#include <groq_stt.h>

// =============================================================================
// 1. Wi-Fi & Groq API Credentials
// =============================================================================
const char* WIFI_SSID    = "YOUR_WIFI";      // your Wi-Fi network name
const char* WIFI_PASS    = "YOUR_PASSWORD";  // your Wi-Fi password
const char* GROQ_API_KEY = "gsk_...";        // your Groq API key (starts with gsk_)

// =============================================================================
// 2. Hardware Pin Configuration (Change to match YOUR custom board/wiring)
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

// Button pin for Push-to-Talk:
// Set to 0 to use onboard BOOT button, or any GPIO connected to a button to GND.
// Set to -1 if your project has NO button (and use hands-free VAD below).
const int PIN_BUTTON  = 0;

// Initialize GroqSTT with our explicit pins (never hardcoded to any single board)
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

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=============================================");
  Serial.println("         GroqSTT - BasicTranscribe           ");
  Serial.println("=============================================");

  connectWiFi();

  // ===========================================================================
  // 3. Complete Library Configuration & Tuning (All settings exposed!)
  // ===========================================================================

  // --- A. Model Selection ---
  // Groq offers 3 official Whisper models:
  //   1. "whisper-large-v3-turbo" (Default & Recommended):
  //      - Ultra-fast sub-second latency (~200 - 400 ms inference)
  //      - Multilingual support for 99 languages
  //      - Best balance of speed, accuracy, and lowest cost ($0.04/hr)
  //   2. "whisper-large-v3":
  //      - Full Large v3 model (~700 - 1200 ms inference)
  //      - Top accuracy for heavy accents, technical vocabulary, or noisy audio
  //   3. "distil-whisper-large-v3-en":
  //      - Distilled, lightweight model for English-only applications ($0.02/hr)
  stt.setModel("whisper-large-v3-turbo");

  // --- B. Language ---
  // Set explicit ISO-639-1 code (e.g., "en", "es", "fr", "de", "hi", "zh", "ja").
  // Set to "" (empty string) to enable Whisper's automatic language detection!
  stt.setLanguage("en");

  // --- C. Audio & DSP Tuning ---
  // Digital Gain: Multiplier applied after filtering (default: 8, range: 1 to 16).
  //   - Increase (e.g. 10-12) if speaking from a distance or mic is quiet.
  //   - Decrease (e.g. 4-6) if speaking very close or in loud environments.
  stt.setGain(8);

  // High-Pass Filter: Corner frequency in Hz (default: 120 Hz).
  //   - Strips sub-audible DC offset and mechanical desk rumble from INMP441,
  //     freeing up 26x of digital headroom for crystal-clear vocal capture.
  stt.setHighpassHz(120);

  // Silence Threshold: Minimum audio peak (0 - 32767) required to upload (default: 300).
  //   - Prevents Whisper from hallucinating phantom words when no one is speaking.
  stt.setSilenceThreshold(300);

  // --- D. Context Prompt (Optional) ---
  // Pass keywords or specialized acronyms to help Whisper spell them properly:
  // stt.setPrompt("ESP32, GroqSTT, Arduino, IoT, Neopixel");

  // --- E. Hands-free VAD (Voice Activity Detection) ---
  // Want hands-free recording with NO buttons?
  // Uncomment the two lines below:
  // stt.setButtonPin(-1); // disable button
  // stt.useVad(true);     // auto-stop recording ~1.2s after you stop speaking

  // Initialize GroqSTT with API key
  if (!stt.begin(GROQ_API_KEY)) {
    Serial.print("[INIT FAIL] ");
    Serial.println(stt.errorText());
    while (true) delay(1000);
  }

  Serial.println("[INIT OK] Ready!");
  if (PIN_BUTTON >= 0) {
    Serial.println(">> Press & HOLD button to talk, RELEASE to transcribe <<\n");
  } else {
    Serial.println(">> Speak freely into the microphone (Hands-free VAD enabled) <<\n");
  }
}

void loop() {
  // listen() handles everything:
  //   - Pre-warms the TLS socket in the background while waiting
  //   - Detects the button press (or starts immediately if buttonless/VAD)
  //   - Filters and streams audio chunks in real-time
  //   - Applies 200ms button release debounce
  //   - Returns the transcribed text string
  String text = stt.listen();

  if (text.length() > 0) {
    Serial.println("---------------------------------------------");
    Serial.print("Transcript: \"");
    Serial.print(text);
    Serial.println("\"");
    Serial.printf("Latency   : %.0f ms (Upload: %.0f ms | Inference: %.0f ms)\n",
                  stt.lastLatencyMs(), stt.lastSendMs(), stt.lastInferMs());
    Serial.println("---------------------------------------------\n");
  } else if (stt.lastError() != STT_OK) {
    Serial.print("[ERROR] ");
    Serial.print(stt.errorText());
    Serial.printf(" (HTTP %u)\n\n", stt.lastHttpStatus());
  }
}
