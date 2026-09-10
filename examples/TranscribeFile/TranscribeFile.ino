/*
  TranscribeFile - send a WAV (or raw PCM) from SD or LittleFS to Groq, no mic needed.

  Useful for:
    - batch-processing audio files (recordings, podcasts, voice notes)
    - testing the library without speaking
    - making a stand-alone "transcribe a file" tool

  Files stream straight from storage - no full-utterance RAM buffer. The
  library detects the file type:
    - a real WAV (RIFF/WAVE header) passes through untouched
    - any other file is treated as raw 16-bit mono PCM at 16 kHz, and a
      44-byte WAV header is prepended automatically

  Works with any fs::FS: SD, LittleFS, SPIFFS.

  No audio file handy? This sketch synthesizes a 1-second 1 kHz beep WAV on
  LittleFS and transcribes it, so the upload path is exercised end to end.

  Hardware: ESP32 / ESP32-S3 (no microphone needed).
  Get a free API key at console.groq.com/keys
*/

#include <Arduino.h>        // Serial, delay(), millis()
#include <SPI.h>            // SD card SPI bus
#include <LittleFS.h>       // on-board flash filesystem (no extra wiring)
#include <SD.h>             // SD card over SPI
#include <WiFi.h>           // ESP32 Wi-Fi stack
#include <groq_stt.h>       // the library (transcribeFile() is what we use)

// --- Credentials ------------------------------------------------------------
const char* WIFI_SSID    = "YOUR_WIFI";
const char* WIFI_PASS    = "YOUR_PASSWORD";
const char* GROQ_API_KEY = "gsk_...";

// --- Library instance --------------------------------------------------------
GroqSTT stt;

// --- File locations to look for ---------------------------------------------
// Put a 16 kHz mono WAV on the SD card at SD_WAV_PATH, OR upload one into
// LittleFS at LFS_WAV_PATH via the Arduino IDE "Sketch Data Upload" plugin
// (or the PlatformIO equivalent).
const char* SD_WAV_PATH  = "/test.wav";
const char* LFS_WAV_PATH = "/test.wav";

// --- Connect to Wi-Fi ourselves --------------------------------------------
static void connectWiFi() {
  Serial.print("[NET] connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("[NET] connected, IP: ");
  Serial.println(WiFi.localIP());
}

// --- Synthesize a 1-second 1 kHz sine wave into a WAV file ----------------
// WAV = 44-byte RIFF/WAVE header + raw 16-bit mono PCM samples.
// We write 16000 samples (1 second at 16 kHz) of sin(2*pi*1000*t).
// This is only used when the user has neither an SD card nor a LittleFS file.
static void writeSineWav(fs::File& f, uint32_t rate, uint16_t seconds) {
  uint32_t nSamples = rate * seconds;       // total samples
  uint32_t dataLen  = nSamples * 2;         // each sample is 2 bytes
  uint8_t  h[44] = {0};

  // Build the RIFF/WAVE header in a small buffer.
  memcpy(h,     "RIFF", 4);                 // magic
  uint32_t v = dataLen + 36;                // total file size minus 8
  memcpy(h + 4, &v, 4);
  memcpy(h + 8, "WAVE", 4);                 // format
  memcpy(h + 12, "fmt ", 4);                // "fmt " subchunk
  v = 16; memcpy(h + 16, &v, 4);            // PCM subchunk size
  uint16_t s = 1; memcpy(h + 20, &s, 2);    // PCM format
  s = 1;      memcpy(h + 22, &s, 2);        // mono
  memcpy(h + 24, &rate, 4);                 // sample rate
  v = rate * 2; memcpy(h + 28, &v, 4);      // byte rate (rate * channels * bits/8)
  s = 2; memcpy(h + 32, &s, 2);            // block align
  s = 16; memcpy(h + 34, &s, 2);            // bits per sample
  memcpy(h + 36, "data", 4);               // "data" subchunk
  memcpy(h + 40, &dataLen, 4);             // data length in bytes
  f.write(h, 44);

  // Now write the actual PCM samples: sin(2*pi*1000*t) scaled to int16.
  for (uint32_t i = 0; i < nSamples; i++) {
    int16_t sample = (int16_t)(sinf(2.0f * PI * 1000.0f * i / rate) * 6000.0f);
    f.write((const uint8_t*)&sample, 2);
  }
}

// --- Send one file to Groq and print the result ----------------------------
static void transcribe(const char* label, fs::FS& fs, const char* path) {
  Serial.print("[FILE] source: ");
  Serial.print(label);
  Serial.print("  ");
  Serial.println(path);

  // stt.transcribeFile() opens the file, streams it to Groq, and returns
  // the transcript text. It returns "" on error (check stt.lastError()).
  uint32_t start = millis();
  String text = stt.transcribeFile(fs, path);
  uint32_t total = millis() - start;

  if (text.length()) {
    // Success. The model heard a 1 kHz sine wave, so we expect it to come
    // back with something like a short "BEEP" or "TONE" - or possibly empty
    // (whisper often stays quiet on pure tones).
    Serial.print("Transcript: ");
    Serial.println(text);
  } else {
    // Error path - usually wrong path, unreadable file, or network problem.
    Serial.print("Error: ");
    Serial.println(stt.errorText());
    Serial.print("HTTP status: ");
    Serial.println(stt.lastHttpStatus());
  }

  // Last-latency timing: time from "request head sent" to "reply received".
  // For a file this is dominated by the upload of the audio bytes.
  Serial.print("Time: ");
  Serial.print(total);
  Serial.println(" ms");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("GroqSTT - TranscribeFile");
  Serial.print("Model: ");
  Serial.println(GROQ_STT_MODEL);
  Serial.print("Endpoint: ");
  Serial.print(GROQ_STT_HOST);
  Serial.println(GROQ_STT_PATH);

  connectWiFi();

  if (!stt.begin(GROQ_API_KEY)) {
    Serial.print("[INIT] failed: ");
    Serial.println(stt.errorText());
    while (true) delay(1000);
  }
  Serial.println("[INIT] ready");

  // Pick the first source that has a file, in this order:
  //   1. a real WAV on the SD card
  //   2. a real WAV already uploaded to LittleFS
  //   3. synthesize a 1-second 1 kHz beep into LittleFS and use that
  //      (so the example always has something to send, even on a fresh board)

  if (SD.begin(SS)) {                       // try the SD card first
    if (SD.exists(SD_WAV_PATH)) {
      transcribe("SD", SD, SD_WAV_PATH);
      SD.end();
      return;
    }
    SD.end();
  }

  if (!LittleFS.begin()) {                  // then an uploaded LittleFS file
    Serial.println("[FS] LittleFS mount failed");
    return;
  }
  if (LittleFS.exists(LFS_WAV_PATH)) {
    transcribe("LittleFS", LittleFS, LFS_WAV_PATH);
    return;
  }

  // No file anywhere - synthesize a test WAV and send that.
  Serial.println("[FS] no audio file - writing a 1 kHz test beep to LittleFS");
  fs::File f = LittleFS.open(LFS_WAV_PATH, "w");
  if (f) {
    writeSineWav(f, 16000, 1);
    f.close();
    transcribe("LittleFS", LittleFS, LFS_WAV_PATH);
  } else {
    Serial.println("[FS] failed to write test.wav");
  }
}

void loop() {
  // All the work happens in setup() - transcribeFile is blocking. After it
  // returns, there's nothing more to do; sleep forever to save power.
  delay(60000);
}
