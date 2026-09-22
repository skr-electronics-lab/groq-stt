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

// Initialize GroqSTT with microphone and button pins
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
  // 3. Configuration & Audio Tuning
  // ===========================================================================

  // --- A. Model Selection ---
  // "whisper-large-v3-turbo" : Fast sub-second response (~200-400ms), multilingual (Default).
  // "whisper-large-v3"       : Maximum accuracy for heavy accents & translation.
  stt.setModel("whisper-large-v3-turbo");

  // --- B. Language ---
  // ISO code ("en", "es", "hi", etc.) or "" for auto-detection.
  stt.setLanguage("en");

  // --- C. Audio Tuning ---
  // Digital Gain: 1 to 16 (default 8). Boosts volume after filtering.
  stt.setGain(8);

  // High-Pass Filter: Cuts frequencies below 120 Hz (default 120 Hz).
  // Strips DC offset and desk/fan vibrations from the INMP441, freeing up digital
  // headroom so your voice can be amplified cleanly without distortion.
  stt.setHighpassHz(120);

  // Silence Threshold: Minimum volume peak (0-32767) needed to upload (default 300).
  stt.setSilenceThreshold(300);

  // --- D. Context Prompt (Optional) ---
  // Biases Whisper toward specific terms, short commands, or Hinglish:
  // stt.setPrompt("ESP32, Groq, INMP441, Neopixel, turn on, red");

  // --- E. Hands-free VAD Tuning (No Button) ---
  // stt.setButtonPin(-1);        // Disable button for hands-free
  // stt.useVad(true);            // Auto-stop when you stop talking
  // stt.setVadSilenceMs(1200);   // Trailing silence pause before upload (ms)
  // stt.setVadSpeechRatio(2.0f); // Voice sensitivity (1.5 = quiet room, 2.5+ = noisy)

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
