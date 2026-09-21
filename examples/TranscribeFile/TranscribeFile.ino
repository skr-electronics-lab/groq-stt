/*
  TranscribeFile - Stream and transcribe WAV files from LittleFS, SD, or SPIFFS

  Useful for:
    - Transcribing stored voice notes, audio recordings, or sound logs
    - Standalone batch audio processing
    - Testing Groq STT connectivity without needing a microphone wired up!

  How it works:
    - Audio streams chunk-by-chunk directly from flash/SD into TLS (RAM usage is < 2 KB).
    - Supports standard 16 kHz 16-bit mono WAV files.
    - If no file exists, this sketch automatically synthesizes a sample WAV on LittleFS
      so the entire Groq cloud pipeline is exercised end-to-end.

  Hardware: ESP32 or ESP32-S3 (Zero microphone wiring needed!).
  Get a free Groq API key at: https://console.groq.com/keys
*/

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <SD.h>
#include <groq_stt.h>

// =============================================================================
// 1. Wi-Fi & Groq API Credentials
// =============================================================================
const char* WIFI_SSID    = "YOUR_WIFI";
const char* WIFI_PASS    = "YOUR_PASSWORD";
const char* GROQ_API_KEY = "gsk_...";

// Audio file paths to search for:
const char* LFS_WAV_PATH = "/test.wav";
const char* SD_WAV_PATH  = "/test.wav";

// No pins needed in constructor since we're transcribing from storage!
GroqSTT stt;

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

// --- Synthesize a 1-second 1 kHz test beep WAV file on LittleFS --------------
static void writeSineWav(fs::File& f, uint32_t rate, uint16_t seconds) {
  uint32_t nSamples = rate * seconds;
  uint32_t dataLen  = nSamples * 2;
  uint8_t  h[44] = {0};

  memcpy(h,      "RIFF", 4);
  uint32_t v = dataLen + 36;
  memcpy(h + 4,  &v, 4);
  memcpy(h + 8,  "WAVE", 4);
  memcpy(h + 12, "fmt ", 4);
  v = 16; memcpy(h + 16, &v, 4);
  uint16_t s = 1; memcpy(h + 20, &s, 2);
  s = 1;  memcpy(h + 22, &s, 2);
  memcpy(h + 24, &rate, 4);
  v = rate * 2; memcpy(h + 28, &v, 4);
  s = 2;  memcpy(h + 32, &s, 2);
  s = 16; memcpy(h + 34, &s, 2);
  memcpy(h + 36, "data", 4);
  memcpy(h + 40, &dataLen, 4);
  f.write(h, 44);

  for (uint32_t i = 0; i < nSamples; i++) {
    int16_t sample = (int16_t)(sinf(2.0f * PI * 1000.0f * i / rate) * 8000.0f);
    f.write((const uint8_t*)&sample, 2);
  }
}

static void runFileTranscription(const char* label, fs::FS& fs, const char* path) {
  Serial.printf("\n[FILE] Transcribing from %s: %s\n", label, path);

  uint32_t start = millis();
  String transcript = stt.transcribeFile(fs, path);
  uint32_t total = millis() - start;

  if (transcript.length() > 0) {
    Serial.println("---------------------------------------------");
    Serial.printf("Transcript : \"%s\"\n", transcript.c_str());
    Serial.printf("Total Time : %u ms (Inference: %.0f ms)\n", total, stt.lastInferMs());
    Serial.println("---------------------------------------------\n");
  } else if (stt.lastError() != STT_OK) {
    Serial.printf("[ERROR] %s (HTTP %u)\n\n", stt.errorText(), stt.lastHttpStatus());
  } else {
    Serial.println("[RESULT] Audio recognized (silence or pure tone returned empty string)\n");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=============================================");
  Serial.println("          GroqSTT - TranscribeFile           ");
  Serial.println("=============================================");

  connectWiFi();

  // ===========================================================================
  // 2. Complete Library Configuration & Tuning
  // ===========================================================================

  // Model: "whisper-large-v3-turbo" (Fastest, default),
  //        "whisper-large-v3" (Best for complex domain audio),
  //        "distil-whisper-large-v3-en" (English only)
  stt.setModel("whisper-large-v3-turbo");

  // Language: Set explicit ISO code ("en", "es", "fr", etc.) or "" for auto-detection
  stt.setLanguage("en");

  // Response Format: STT_FMT_TEXT (default), STT_FMT_JSON, or STT_FMT_VERBOSE_JSON
  stt.setResponseFormat(STT_FMT_TEXT);

  // Optional spelling prompt:
  // stt.setPrompt("ESP32, GroqSTT, Arduino");

  if (!stt.begin(GROQ_API_KEY)) {
    Serial.print("[INIT FAIL] ");
    Serial.println(stt.errorText());
    while (true) delay(1000);
  }

  Serial.println("[INIT OK] Ready!");

  // Try SD card first, then LittleFS
  if (SD.begin(SS) && SD.exists(SD_WAV_PATH)) {
    runFileTranscription("SD Card", SD, SD_WAV_PATH);
    SD.end();
    return;
  }

  if (LittleFS.begin()) {
    if (LittleFS.exists(LFS_WAV_PATH)) {
      runFileTranscription("LittleFS", LittleFS, LFS_WAV_PATH);
      return;
    }

    // If no file exists, synthesize a sample WAV to test Groq API
    Serial.println("[FS] No WAV found on LittleFS - synthesizing sample test.wav...");
    fs::File f = LittleFS.open(LFS_WAV_PATH, "w");
    if (f) {
      writeSineWav(f, 16000, 1);
      f.close();
      runFileTranscription("Synthesized LittleFS", LittleFS, LFS_WAV_PATH);
    }
  }
}

void loop() {
  // File transcription completes in setup(); nothing needed in loop.
  delay(10000);
}
