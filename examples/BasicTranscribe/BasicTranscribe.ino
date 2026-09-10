/*
  BasicTranscribe - the happy path (~5 lines of user code, zero configuration)

  Hold BOOT (GPIO0) and talk; release to transcribe. Text prints to Serial.

  What the library does for you out of the box (no configuration needed):
    - model       whisper-large-v3-turbo  (Groq's speech-to-text)
    - sample rate 16 kHz mono              (what Groq expects)
    - gain x8 + 120 Hz rumble high-pass    (clean mic signal)
    - recording   ends when you release the button  (no time cap)

  Want it hands-free? Wire no button, then call:
      stt.setButtonPin(-1);    // tell the library: "there is no button"
      stt.useVad(true);        // auto-stop ~1.2 s after you stop talking
  See src/groq_stt_config.h for every tunable (gain, model, language, etc).

  Hardware wiring:
    Classic ESP32 (verified rig, arduino-esp32 core 3.x)  - I2S mic on
        SCK=26, WS=25, SD=22
    ESP32-S3                                             - I2S mic on
        SCK=5,  WS=6,  SD=4
    INMP441 connections: VDD -> 3V3 (NOT 5V), GND -> GND,
                         L/R -> GND (left channel), SCK/WS/SD as above.

  Get a free API key at: https://console.groq.com/keys
*/

#include <Arduino.h>      // Serial, delay(), millis(), digitalRead(), String, etc.
#include <WiFi.h>         // ESP32 Wi-Fi stack (we drive the connection ourselves)
#include <groq_stt.h>     // the GroqSTT library (one include gives you everything)

// --- 1. Wi-Fi + API credentials ---------------------------------------------
// Edit these three lines with your own values, then flash.
const char* WIFI_SSID    = "YOUR_WIFI";      // your Wi-Fi network name
const char* WIFI_PASS    = "YOUR_PASSWORD";  // your Wi-Fi password
const char* GROQ_API_KEY = "gsk_...";        // your Groq API key (starts with gsk_)

// --- 2. Create the STT object ------------------------------------------------
// The default constructor picks the right I2S pins for the chip you're on and
// uses GPIO 0 (the BOOT button) as the push-to-talk button.
GroqSTT stt;

// --- 3. Tiny helper: connect to Wi-Fi and report the assigned IP ------------
// We connect Wi-Fi ourselves so the library only has to worry about the mic
// and the upload - that's the "non-interfering" promise. You could skip this
// and call stt.begin(SSID, PASS, KEY) instead and the library would do it.
static void connectWiFi() {
  // Tell the user which network we're trying, so it's obvious in the Serial log.
  Serial.print("[NET] connecting to ");
  Serial.println(WIFI_SSID);

  // Station mode = "join an existing Wi-Fi network" (vs. creating our own AP).
  WiFi.mode(WIFI_STA);
  // Start the connection in the background; we poll WiFi.status() below.
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Block (with small sleeps) until the link is up. We print a dot every
  // 400 ms so the user can see progress in the Serial Monitor.
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }

  // Link is up - print the assigned IP so the user can reach the device
  // (web UI, MQTT, OTA, etc.) if they want to.
  Serial.println();
  Serial.print("[NET] connected, IP: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  // Open the Serial port at 115200 baud (matches the ESP32 bootloader default
  // and most example sketches). delay(500) gives the USB-Serial chip a moment
  // to enumerate on the host so the very first prints don't get lost.
  Serial.begin(115200);
  delay(500);

  // Friendly banner so the user knows which example is running and which
  // Groq model will do the transcribing.
  Serial.println("GroqSTT - BasicTranscribe");
  Serial.print("Model: ");
  Serial.println(GROQ_STT_MODEL);

  // Get on Wi-Fi first; the library needs a working network to reach Groq.
  connectWiFi();

  // stt.begin(apiKey) initializes the library. It does NOT touch Wi-Fi - it
  // only checks that Wi-Fi is up, validates the API key, and prepares the
  // I2S microphone driver. If something went wrong it returns false and we
  // print the human-readable error and stop.
  if (!stt.begin(GROQ_API_KEY)) {
    Serial.print("[INIT] failed: ");
    Serial.println(stt.errorText());
    while (true) delay(1000);   // halt here - nothing useful to do without net
  }

  // Ready. Tell the user what to do.
  Serial.println("[INIT] ready - hold BOOT to talk");
}

void loop() {
  // listen() is self-contained: it waits for you to PRESS BOOT, records
  // while you hold it, streams the audio to Groq, and returns the transcript.
  // Do NOT read the button yourself before calling it - that would eat the
  // press and listen() would then wait for a second one.
  Serial.println("[REC] hold BOOT to talk");
  String text = stt.listen();

  // stt.listen() returns:
  //   - a non-empty String  -> success, here's the transcript
  //   - an empty String     -> something went wrong, see stt.lastError()
  if (text.length()) {
    // Success path. lastLatencyMs() is "request head sent -> reply received",
    // i.e. the real upload + Groq processing time. It does NOT include how
    // long you held BOOT, so it's the number that actually matters.
    Serial.print("Transcript: ");
    Serial.println(text);
    Serial.print("Time: ");
    Serial.print((unsigned)stt.lastLatencyMs());
    Serial.print(" ms  (send ");
    Serial.print((unsigned)stt.lastSendMs());
    Serial.print(" + infer ");
    Serial.print((unsigned)stt.lastInferMs());
    Serial.println(")");
  } else {
    // Error path. Print the human-readable reason and the HTTP status Groq
    // returned (0 if the request never made it out, e.g. Wi-Fi dropped).
    Serial.print("Error: ");
    Serial.println(stt.errorText());
    Serial.print("HTTP status: ");
    Serial.println(stt.lastHttpStatus());
  }
}
