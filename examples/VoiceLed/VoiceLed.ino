/*
  VoiceLed - Control ESP32-S3 RGB LED & Standard LEDs by Voice!

  Talk to your board to change LED colors or toggle them:
    - "Turn on the light"  / "Turn off the light"
    - "Change color to red" / "Set green" / "Make it blue"
    - "Yellow", "Purple", "Cyan", "White"
    - "Blink the LED" / "Flash the light"

  Hardware Compatibility:
    - ESP32-S3: Automatically uses the built-in WS2812 RGB LED (RGB_BUILTIN)!
      No extra libraries needed - works directly with Arduino ESP32 core.
    - Classic ESP32: Uses the onboard single-color LED (GPIO 2 / LED_BUILTIN).

  Microphone Wiring (INMP441 I2S):
    - VDD -> 3.3V  (Do NOT connect to 5V!)
    - GND -> GND
    - L/R -> GND   (Selects the Left audio channel)
    - SD  -> Data  (GPIO 4 on ESP32-S3 | GPIO 22 on classic ESP32)
    - SCK -> Clock (GPIO 5 on ESP32-S3 | GPIO 26 on classic ESP32)
    - WS  -> Word  (GPIO 6 on ESP32-S3 | GPIO 25 on classic ESP32)

  Get a free Groq API key at: https://console.groq.com/keys
*/

#include <Arduino.h>
#include <WiFi.h>
#include <groq_stt.h>

// =============================================================================
// STEP 1: Enter your Wi-Fi Name, Wi-Fi Password, and Groq API Key
// =============================================================================
const char* WIFI_SSID    = "YOUR_WIFI";      // Enter your Wi-Fi name
const char* WIFI_PASS    = "YOUR_PASSWORD";  // Enter your Wi-Fi password
const char* GROQ_API_KEY = "gsk_...";        // Enter your Groq API key

// =============================================================================
// STEP 2: Configure Microphone & Button Pins
// =============================================================================
#if defined(CONFIG_IDF_TARGET_ESP32S3)
// Default I2S pins for ESP32-S3:
const int PIN_MIC_SCK = 5;
const int PIN_MIC_WS  = 6;
const int PIN_MIC_SD  = 4;
#else
// Default I2S pins for Classic ESP32:
const int PIN_MIC_SCK = 26;
const int PIN_MIC_WS  = 25;
const int PIN_MIC_SD  = 22;
#endif

// Button pin for Push-to-Talk (0 is the onboard BOOT button on most ESP32 boards)
const int PIN_BUTTON = 0;

// Standard single-color LED pin (used if board does not have an RGB LED)
#ifdef LED_BUILTIN
const int PIN_LED = LED_BUILTIN;
#else
const int PIN_LED = 2; // Common default for classic ESP32 boards
#endif

// Create the Speech-to-Text object with our custom pins
GroqSTT stt(PIN_MIC_SCK, PIN_MIC_WS, PIN_MIC_SD, PIN_BUTTON);

// Keep track of the current color and state
uint8_t currentR = 0;
uint8_t currentG = 0;
uint8_t currentB = 0;

// =============================================================================
// STEP 3: Helper Function to Set LED Color
// Works for both ESP32-S3 RGB NeoPixel and Classic Single-Color LEDs
// =============================================================================
static void setLedColor(uint8_t r, uint8_t g, uint8_t b, const char* colorName) {
  currentR = r;
  currentG = g;
  currentB = b;

#ifdef RGB_BUILTIN
  // On ESP32-S3, write directly to the onboard RGB NeoPixel:
  neopixelWrite(RGB_BUILTIN, r, g, b);
#else
  // On classic ESP32 with single-color LED: turn ON if any color is active
  digitalWrite(PIN_LED, (r || g || b) ? HIGH : LOW);
#endif

  Serial.printf("[LED] Set to %s (R:%d, G:%d, B:%d)\n", colorName, r, g, b);
}

// Function to blink the LED 3 times
static void blinkLed(int times) {
  Serial.printf("[LED] Blinking %d times...\n", times);
  for (int i = 0; i < times; i++) {
#ifdef RGB_BUILTIN
    neopixelWrite(RGB_BUILTIN, 64, 64, 64); // White flash
#else
    digitalWrite(PIN_LED, HIGH);
#endif
    delay(150);

#ifdef RGB_BUILTIN
    neopixelWrite(RGB_BUILTIN, 0, 0, 0);       // Off
#else
    digitalWrite(PIN_LED, LOW);
#endif
    delay(150);
  }

  // Restore the previous color after blinking
  setLedColor(currentR, currentG, currentB, "Restored Color");
}

// =============================================================================
// STEP 4: Process What You Said & Control the LED
// =============================================================================
static void processVoiceCommand(const String& rawText) {
  String s = rawText;
  s.toLowerCase(); // Convert text to lowercase for easy word matching

  Serial.println("\n---------------------------------------------");
  Serial.printf("You Said: \"%s\"\n", rawText.c_str());

  // Check what action or color was mentioned:
  if (s.indexOf("blink") >= 0 || s.indexOf("flash") >= 0) {
    blinkLed(3);
  } else if (s.indexOf("red") >= 0) {
    setLedColor(64, 0, 0, "RED");
  } else if (s.indexOf("green") >= 0) {
    setLedColor(0, 64, 0, "GREEN");
  } else if (s.indexOf("blue") >= 0) {
    setLedColor(0, 0, 64, "BLUE");
  } else if (s.indexOf("yellow") >= 0) {
    setLedColor(64, 64, 0, "YELLOW");
  } else if (s.indexOf("purple") >= 0 || s.indexOf("magenta") >= 0) {
    setLedColor(64, 0, 64, "PURPLE");
  } else if (s.indexOf("cyan") >= 0 || s.indexOf("sky") >= 0) {
    setLedColor(0, 64, 64, "CYAN");
  } else if (s.indexOf("white") >= 0 || s.indexOf("turn on") >= 0 || s.indexOf("light on") >= 0) {
    setLedColor(64, 64, 64, "WHITE (ON)");
  } else if (s.indexOf("turn off") >= 0 || s.indexOf("switch off") >= 0 || s.indexOf("light off") >= 0) {
    setLedColor(0, 0, 0, "OFF");
  } else {
    Serial.println("[HINT] Command not recognized! Try saying:");
    Serial.println("       - \"Set color to red / green / blue / yellow\"");
    Serial.println("       - \"Blink the LED\"");
    Serial.println("       - \"Turn on the light\" or \"Turn off the LED\"");
  }
  Serial.println("---------------------------------------------\n");
}

// Helper to connect to your Wi-Fi network
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
  Serial.print("[NET] Connected! IP Address: ");
  Serial.println(WiFi.localIP());
}

// =============================================================================
// STEP 5: Arduino Setup
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=============================================");
  Serial.println("       GroqSTT - Voice RGB LED Control       ");
  Serial.println("=============================================");

  // Set single-color LED pin as output
  pinMode(PIN_LED, OUTPUT);

  // Start with LED off
  setLedColor(0, 0, 0, "OFF");

  // Connect to Wi-Fi
  connectWiFi();

  // --- Library Configuration & Tuning ---
  // Model: "whisper-large-v3-turbo" gives fast response under 400 milliseconds
  stt.setModel("whisper-large-v3-turbo");

  // Language: English
  stt.setLanguage("en");

  // Context Prompt: Biases Whisper to recognize short command words accurately:
  stt.setPrompt("red, green, blue, yellow, purple, cyan, white, blink, turn on, turn off, LED, light");

  // Audio Tuning:
  // High-pass (120 Hz) strips desk/fan vibrations, freeing headroom for 8x digital gain.
  stt.setGain(8);
  stt.setHighpassHz(120);
  stt.setSilenceThreshold(300);

  // Hands-free VAD Tuning (Optional - when PIN_BUTTON = -1):
  // stt.setButtonPin(-1);        // Disable button for hands-free
  // stt.useVad(true);            // Auto-stop when you stop talking
  // stt.setVadSilenceMs(1200);   // Trailing silence pause before upload (ms)
  // stt.setVadSpeechRatio(2.0f); // Voice sensitivity (1.5 = quiet room, 2.5+ = noisy)

  // Initialize the speech-to-text library
  if (!stt.begin(GROQ_API_KEY)) {
    Serial.print("[INIT FAILED] ");
    Serial.println(stt.errorText());
    while (true) delay(1000); // Stop if initialization fails
  }

  Serial.println("[INIT OK] Ready!");
  Serial.println(">> Press & HOLD the BOOT button, say your color/command, then RELEASE <<\n");
}

// =============================================================================
// STEP 6: Main Loop
// =============================================================================
void loop() {
  // Keep TLS connection warm in the background for zero-delay response
  stt.prewarm();

  // listen() waits for you to hold the button, records your voice,
  // uploads it directly to Groq, and returns the spoken text
  String text = stt.listen();

  // If speech was successfully captured, process the command
  if (text.length() > 0) {
    processVoiceCommand(text);
  } else if (stt.lastError() != STT_OK) {
    // If an error occurred (e.g. silence or mic disconnected), print error
    Serial.print("[ERROR] ");
    Serial.println(stt.errorText());
  }
}
