/*
  VoiceLed - control the onboard LED by voice: "turn on the led" / "turn off the led"

  This is the "AI assistant" pattern in miniature:
    1. STT (this library) turns your speech into text
    2. your code decides what the text means
    3. your code acts on it (here: a GPIO)
  For a real assistant, send that text to YOUR server and feed the reply to
  whichever TTS you like. The library only ever owns the mic and the upload.

  Hold BOOT, say the command, release. Each utterance costs one Groq API
  call, so say the command in one go and release when done.

  Accepted (case-insensitive, as long as it mentions the light and the action):
    "turn on the led"     "switch on the led"     "turn the led on"
    "turn off the led"    "switch off the led"    "turn the led off"
    "toggle the led"
  Anything else is echoed and ignored.

  LED pin:
    The library auto-picks LED_BUILTIN if the core defines it (which all
    classic ESP32 and S3 cores do). Classic ESP32 dev boards usually route
    the LED to GPIO 2; if yours is elsewhere, change LED_PIN below.

  Hardware: classic ESP32 (SCK=26, WS=25, SD=22, core 3.x)
            or ESP32-S3 (SCK=5, WS=6, SD=4) + INMP441 I2S mic.
  Get a free API key at console.groq.com/keys
*/

#include <Arduino.h>      // pinMode, digitalWrite, delay, millis, String
#include <WiFi.h>         // ESP32 Wi-Fi stack
#include <groq_stt.h>     // the library

// --- Credentials ------------------------------------------------------------
const char* WIFI_SSID    = "YOUR_WIFI";
const char* WIFI_PASS    = "YOUR_PASSWORD";
const char* GROQ_API_KEY = "gsk_...";

// --- LED pin (use LED_BUILTIN if the core defines it; otherwise GPIO 2) -----
#ifdef LED_BUILTIN
  const int LED_PIN = LED_BUILTIN;          // most cores define this
#else
  const int LED_PIN = 2;                    // classic ESP32 DevKitC fallback
#endif

// --- Library instance --------------------------------------------------------
GroqSTT stt;

// --- Track the LED state ourselves so "toggle" can flip it -------------------
bool ledOn = false;

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

// --- Classify the transcript into a command ---------------------------------
// Returns "ON", "OFF", "TOGGLE", or "IGNORE" (not a light command).
// We do a simple keyword scan on the lowercased text - good enough for the
// small set of phrasings the model tends to return.
static const char* classify(const String& text) {
  String s = text;
  s.toLowerCase();                          // case-insensitive matching

  // If neither "led" nor "light" is mentioned, it isn't a light command.
  if (s.indexOf("led") < 0 && s.indexOf("light") < 0) return "IGNORE";

  // Order matters: check "off" before "on" because "off" contains "on" - we
  // want "turn off" to win, not "turn on".
  if (s.indexOf("toggle") >= 0) return "TOGGLE";
  if (s.indexOf("off")     >= 0) return "OFF";
  if (s.indexOf("on")      >= 0) return "ON";
  return "UNKNOWN";                         // mentioned a light but no action
}

// --- Set the LED and report the new state -----------------------------------
static void setLed(bool on) {
  ledOn = on;
  // Most boards: HIGH = LED on. Some boards wire the LED active-low; flip
  // the ternary if yours does.
  digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("GroqSTT - VoiceLed");
  Serial.print("Model: ");
  Serial.println(GROQ_STT_MODEL);

  connectWiFi();

  // Configure the LED as an output and make sure it starts OFF so the
  // boot state is predictable.
  pinMode(LED_PIN, OUTPUT);
  setLed(false);

  if (!stt.begin(GROQ_API_KEY)) {
    Serial.print("[INIT] failed: ");
    Serial.println(stt.errorText());
    while (true) delay(1000);
  }

  Serial.println("[INIT] ready - say \"turn on the led\" / \"turn off the led\" / \"toggle the led\"");
}

void loop() {
  // Time the round-trip so the user can see latency.
  uint32_t start = millis();
  String text = stt.listen();              // hold BOOT, talk, release
  uint32_t total = millis() - start;

  // Always echo the transcript so the user can see what the model heard.
  Serial.print("Transcript: ");
  Serial.println(text);

  // Decide what to do with it.
  const char* cmd = classify(text);
  Serial.print("Command: ");
  Serial.println(cmd);

  if      (strcmp(cmd, "ON")     == 0) setLed(true);
  else if (strcmp(cmd, "OFF")    == 0) setLed(false);
  else if (strcmp(cmd, "TOGGLE") == 0) setLed(!ledOn);   // flip current state
  else if (strcmp(cmd, "IGNORE") == 0) Serial.println("(not a light command - ignored)");
  else                                  Serial.println("(say \"turn on the led\" or \"turn off the led\")");

  // Show the resulting LED state and the round-trip time.
  Serial.print("LED: ");
  Serial.println(ledOn ? "ON" : "OFF");
  Serial.print("Time: ");
  Serial.print(total);
  Serial.println(" ms");
}
