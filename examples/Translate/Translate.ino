/*
  Translate - non-English speech -> English text via Groq /audio/translations

  This example shows three library features together:
    1. setTranslate(true)        - routes to /audio/translations; the library
                                    auto-switches the model to whisper-large-v3
                                    (the only model Groq allows for translate)
    2. setResponseFormat(VERBOSE) - asks Groq for a JSON reply with per-segment
                                    start/end timestamps (finest granularity
                                    Groq exposes; no word-level timestamps)
    3. setTemperature(0.8)        - whisper's "determinism" dial. 0 = greedy
                                    (default), higher = more varied output

  Press BOOT, speak any language, release. The English translation plus the
  segment timestamps print to Serial.

  This example uses ArduinoJson (the only external dep in any example) to
  parse the verbose JSON reply.

  Hardware: classic ESP32 (SCK=26, WS=25, SD=22, core 3.x)
            or ESP32-S3 (SCK=5, WS=6, SD=4) + INMP441 I2S mic.
  Get a free API key at console.groq.com/keys
*/

#include <Arduino.h>          // Serial, delay(), millis(), String
#include <WiFi.h>             // ESP32 Wi-Fi stack
#include <groq_stt.h>         // the library
#include <ArduinoJson.h>      // tiny JSON parser, only for verbose JSON replies

// --- Credentials -------------------------------------------------------------
const char* WIFI_SSID    = "YOUR_WIFI";
const char* WIFI_PASS    = "YOUR_PASSWORD";
const char* GROQ_API_KEY = "gsk_...";

// --- The library instance ----------------------------------------------------
GroqSTT stt;

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

// --- Parse the verbose-JSON reply and print it nicely -----------------------
// stt.listen() returns the raw response body. With STT_FMT_VERBOSE_JSON that
// body is a JSON object containing: task, language, text, and a segments[].
// We deserialize it with ArduinoJson and print the parts we care about.
static void printReply(const String& body) {
  // ArduinoJson's JsonDocument is a RAM arena; 2 KB is plenty for Groq's
  // typical translation reply.
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {                                // malformed JSON (shouldn't happen)
    Serial.print("JSON parse error: ");
    Serial.println(err.c_str());
    return;
  }

  // "task"      is "translate" (we set translate=true) or "transcribe"
  // "language"  is the language the model detected in the source audio
  // "text"      is the full English translation
  Serial.print("Task: ");
  Serial.println(doc["task"] | "");         // the "| "" " fallback returns "" if missing
  Serial.print("Language: ");
  Serial.println(doc["language"] | "");

  Serial.print("Transcript: ");
  Serial.print(doc["text"] | "");           // the translated English
  Serial.println();

  // Segments with start/end in seconds. Iterate the array with a range-for
  // and print each one on its own line.
  Serial.println("Segments:");
  for (JsonVariantConst s : doc["segments"].as<JsonArrayConst>()) {
    Serial.print("  [");
    Serial.print(s["start"] | 0.0f, 2);      // 2 decimal places = 10 ms resolution
    Serial.print(" -> ");
    Serial.print(s["end"]   | 0.0f, 2);
    Serial.print("]  ");
    Serial.println(s["text"] | "");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("GroqSTT - Translate");
  Serial.print("Model: ");
  Serial.print(GROQ_STT_TRANSLATE_MODEL);    // whisper-large-v3 (the only one that translates)
  Serial.print("   endpoint: ");
  Serial.println(GROQ_STT_PATH_TRANSLATE);   // /openai/v1/audio/translations

  connectWiFi();

  // Configure the library for translation with verbose JSON timestamps.
  // These calls go BEFORE stt.begin() so the first request already uses them.
  stt.setTranslate(true);                   // use /audio/translations
  stt.setLanguage("");                      // "" = auto-detect the spoken language
  stt.setTemperature(0.8f);                 // small amount of variation
  stt.setResponseFormat(STT_FMT_VERBOSE_JSON);  // ask for per-segment timestamps
  // stt.setPrompt("This is a technical demo."); // optional spelling/terminology hints

  if (!stt.begin(GROQ_API_KEY)) {
    Serial.print("[INIT] failed: ");
    Serial.println(stt.errorText());
    while (true) delay(1000);
  }

  Serial.println("[INIT] ready - hold BOOT, speak any language, release");
}

void loop() {
  // Time the whole round-trip so the user can see the latency.
  uint32_t start = millis();
  String body = stt.listen();               // record + upload + reply
  uint32_t total = millis() - start;

  // On error, stt.listen() returns "" and sets stt.lastError().
  if (stt.lastError() != STT_OK) {
    Serial.print("Error: ");
    Serial.println(stt.errorText());
    Serial.print("HTTP status: ");
    Serial.println(stt.lastHttpStatus());
  } else {
    // body is the verbose-JSON reply. Parse and print it.
    printReply(body);
    Serial.print("Time: ");
    Serial.print(total);
    Serial.println(" ms");
  }
  delay(1500);                              // small pause so the user can read
}
