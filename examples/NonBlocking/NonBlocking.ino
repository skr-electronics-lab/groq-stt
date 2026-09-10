/*
  NonBlocking - drive the state machine from your own loop() with callbacks

  Use this pattern when your project has other things to do while the user is
  talking (a display, a web server, a button matrix, an MQTT client...).
  tick() does only a small bounded amount of work per call, so it's safe to
  call at any interval - your loop never stalls.

  The default constructor sets BOOT (GPIO 0) as the push-to-talk button.
  Press it: recording starts. Release it: recording stops and the transcript
  arrives asynchronously in the onDone callback.

  Want it hands-free (no button)?
      stt.setButtonPin(-1);    // no button
      stt.useVad(true);        // stop ~1.2 s after you stop talking
  See src/groq_stt_config.h for every tunable.

  Hardware: classic ESP32 (SCK=26, WS=25, SD=22, core 3.x)
            or ESP32-S3 (SCK=5, WS=6, SD=4) + INMP441 I2S mic.

  Get a free API key at console.groq.com/keys
*/

#include <Arduino.h>      // delay(), millis(), digitalRead(), Serial, String
#include <WiFi.h>         // ESP32 Wi-Fi stack
#include <groq_stt.h>     // the library: state machine, callbacks, mic driver

// --- Credentials: edit these three lines for your network + API key ---------
const char* WIFI_SSID    = "YOUR_WIFI";
const char* WIFI_PASS    = "YOUR_PASSWORD";
const char* GROQ_API_KEY = "gsk_...";

// --- BOOT button pin (GPIO 0 is the BOOT button on classic ESP32 and S3) -----
const int BTN_PIN = 0;

// --- The library instance ----------------------------------------------------
GroqSTT stt;

// --- 1. Connect to Wi-Fi ourselves (the library won't touch Wi-Fi) ---------
// We do Wi-Fi here so the example mirrors the "library never touches your
// network" promise. The library only verifies the link is up.
static void connectWiFi() {
  Serial.print("[NET] connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);                       // station mode (join an AP)
  WiFi.begin(WIFI_SSID, WIFI_PASS);          // start connecting in background

  while (WiFi.status() != WL_CONNECTED) {    // block until the link is up
    delay(400);
    Serial.print(".");                       // progress dot in the Serial log
  }

  Serial.println();
  Serial.print("[NET] connected, IP: ");
  Serial.println(WiFi.localIP());
}

// --- 2. Callback: fired when a transcript lands (non-blocking) ---------------
// This runs from inside stt.tick() the moment Groq's reply is fully parsed.
// Keep it short - the library moves on to the IDLE state right after.
void onTranscript(const String& text) {
  // stt.lastLatencyMs() = time from "request head sent" to "reply received".
  // That is the network + Groq processing time, not including how long the
  // user held the button.
  Serial.print("Transcript: ");
  Serial.println(text);
  Serial.print("Time: ");
  Serial.print((unsigned)stt.lastLatencyMs());
  Serial.println(" ms");
}

// --- 3. Callback: fired on any error (network drop, short recording, etc.) ---
void onError(groq_stt_err_t err, const String& detail) {
  // groq_stt_errorText(err) returns a human-readable reason like
  // "TLS failed", "recording too short", "HTTP 401 Unauthorized", etc.
  // detail adds context if the library has any.
  Serial.print("Error: ");
  Serial.print(groq_stt_errorText(err));
  if (detail.length()) {                     // include the extra detail if any
    Serial.print(" - ");
    Serial.print(detail);
  }
  Serial.println();
  Serial.print("HTTP status: ");
  Serial.println(stt.lastHttpStatus());
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("GroqSTT - NonBlocking");
  Serial.print("Model: ");
  Serial.println(GROQ_STT_MODEL);

  connectWiFi();

  // Register the callbacks BEFORE stt.begin() so the library can fire them
  // any time after begin() returns. Callbacks are optional - you can also
  // just poll stt.isDone() and read stt.getResult() from your loop.
  stt.onDone(onTranscript);
  stt.onError(onError);

  // The library init: Wi-Fi is already up, so it just validates the key and
  // prepares the I2S microphone driver. No background work starts here.
  if (!stt.begin(GROQ_API_KEY)) {
    Serial.print("[INIT] failed: ");
    Serial.println(stt.errorText());
    while (true) delay(1000);
  }

  Serial.println("[INIT] ready - press BOOT to talk");
}

void loop() {
  // -----------------------------------------------------------------------
  // YOUR PROJECT'S WORK GOES HERE.
  // Anything that doesn't block longer than a few ms is fine - the library
  // only runs while you call stt.tick() at the bottom of the loop.
  // -----------------------------------------------------------------------
  static uint32_t lastBeat = 0;
  if (millis() - lastBeat > 1000) {          // 1 Hz heartbeat so the user
    lastBeat = millis();                     // can see the loop is alive
    Serial.print(".");
  }

  // Only start a new recording when the library is idle (not already
  // recording or waiting for a reply). isBusy() is true from
  // startRecording() until the transcript (or error) has been delivered.
  if (!stt.isBusy() && digitalRead(BTN_PIN) == LOW) {
    delay(30);                               // simple debounce
    if (digitalRead(BTN_PIN) == LOW) {
      stt.startRecording();                  // also consumes a previous DONE state
      Serial.println();
      Serial.println("[REC] recording... release to transcribe");
    }
  }

  // tick() drives the state machine forward by one step:
  //   IDLE      -> does nothing, returns false (we can stop calling tick)
  //   RECORDING -> reads a chunk of mic audio, streams it to Groq
  //   FINISHING -> sends the terminating chunk
  //   READING   -> pulls a few bytes of the Groq reply, parses them
  //   DONE      -> fires onDone(), returns false
  //   ERROR     -> fires onError(), returns false
  // It is non-blocking: each call is bounded and quick, so your loop stays
  // responsive to other work.
  stt.tick();
  delay(1);
}
