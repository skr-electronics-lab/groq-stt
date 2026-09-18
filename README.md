# GroqSTT

> Push-to-talk speech-to-text for **ESP32 / ESP32-S3** powered by [Groq Whisper](https://console.groq.com).
> One call records, streams, and returns the transcript. No buffering, no time cap, no cloud SDK to wire up.

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Arduino](https://img.shields.io/badge/Arduino-Library-00979D.svg)](https://www.arduino.cc/reference/en/libraries/)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-lib_deps-yellow.svg)](https://platformio.org/)
[![ESP32](https://img.shields.io/badge/ESP32-classic%20%2B%20S3-E7352C.svg)](https://www.espressif.com/)

---

## Why GroqSTT

Most "talk-to-ESP32" projects glue together three libraries, two cores, and a
custom HTTP wrapper. GroqSTT is **one library** that does the whole job:
record from an I2S microphone, chunk-stream the WAV straight to
`api.groq.com/openai/v1/audio/transcriptions`, and hand you the text when
Groq's Whisper returns.

- **One API**: `stt.listen()` blocks, `stt.startRecording() / tick()` doesn't — your loop keeps running.
- **No RAM blow-up**: audio is uploaded as it is captured, so multi-minute commands work on plain (non-PSRAM) ESP32.
- **No fixed time cap**: hold the button as long as you like, release when done. (Optional `setMaxSeconds()` ceiling if you want one.)
- **VAD opt-in**: with no button wired, `useVad(true)` makes recording stop ~1.2 s after you stop talking.
- **Translation, temperature, verbose JSON, segment timestamps** — every knob Groq exposes.
- **Both Arduino cores** (2.0.x legacy I2S and 3.x `ESP_I2S`). Core 2.x reads the INMP441 as silence; use 3.x for INMP441.
- **Zero external dependencies** unless you parse the verbose JSON yourself (then `ArduinoJson` only).

---

## Quick start

```cpp
#include <groq_stt.h>
#include <WiFi.h>

const char* WIFI_SSID     = "your-wifi";
const char* WIFI_PASS     = "your-pass";
const char* GROQ_API_KEY  = "gsk_...";        // console.groq.com/keys

GroqSTT stt;                                  // default pins per chip + BOOT button

void setup() {
  Serial.begin(115200);
  if (!stt.begin(WIFI_SSID, WIFI_PASS, GROQ_API_KEY)) {
    Serial.println(stt.errorText());
    while (1) delay(1000);
  }
}

void loop() {
  String text = stt.listen();                 // hold BOOT, talk, release
  if (text.length()) Serial.println(text);    // "Hello, this is a test"
}
```

That's it. Compile, flash, hold BOOT, speak. With default pins + `LED_BUILTIN`
the library needs **no wiring other than the INMP441** on the documented
SCK/WS/SD pins.

### Hands-free (VAD)

```cpp
stt.setButtonPin(-1);                         // no button
stt.useVad(true);                             // stop ~1.2 s after silence
stt.setVadSilenceMs(2000);                    // optional longer trailing pause
stt.setMaxSeconds(60);                        // optional hard ceiling
```

VAD tracks the ambient noise floor — no fixed threshold to tune for your room.

---

## Installation

| Path | How |
|---|---|
| **Arduino IDE** | Sketch → Include Library → Manage Libraries → search **"GroqSTT"** → Install |
| **Arduino IDE (manual)** | Copy this folder into `<Arduino>/libraries/groq-stt` and restart the IDE |
| **PlatformIO** | Already a valid PIO library — just `lib_deps = skr-electronics-lab/groq-stt` (or `lib_deps = https://github.com/skr-electronics-lab/groq-stt`) |

---

## Examples

| Example | What it shows |
|---|---|
| **`BasicTranscribe`** | Minimal zero-config starter. Hold BOOT, talk, release. |
| **`NonBlocking`**   | `startRecording() / tick()` driven from your own loop with `onDone` / `onError` callbacks. |
| **`Translate`**     | Non-English speech → English via `setTranslate(true)`, with `verbose_json` segment timestamps. |
| **`TranscribeFile`**| Streams a WAV/PCM from SD or LittleFS to Groq — no mic needed. |
| **`VoiceLed`**      | "turn on the led" / "turn off the led" → onboard LED. The "AI assistant" pattern in miniature. |

All examples are designed to **not interfere with your own project code**:
the library never takes over your Wi-Fi unless you ask it to (`begin(ssid,pass,key)`)
and the non-blocking API runs alongside whatever else your loop is doing.

---

## API at a glance

### Lifecycle

```cpp
bool stt.begin(const char* apiKey);                           // user already connected Wi-Fi
bool stt.begin(const char* ssid, const char* pass,
               const char* apiKey);                           // library connects Wi-Fi itself
void stt.end();
```

### Recording

```cpp
String stt.listen();                            // block while BOOT held, return on release
String stt.listen(uint32_t ms);                 // fixed-duration recording
bool   stt.startRecording();                    // non-blocking start
void   stt.stopRecording();                     // force-stop mid-record
bool   stt.tick();                              // pump the state machine (safe at any cadence)
bool   stt.isBusy();
bool   stt.isRecording();
bool   stt.isDone();
String stt.getResult();
```

### Callbacks

```cpp
stt.onDone([](const String& text) { ... });     // fired on a successful result
stt.onError([](groq_stt_err_t e, const String& detail) { ... });
stt.onLevel([](float rms01) { ... });           // per-chunk audio level (0..1)
```

### Output modes

```cpp
stt.setModel("whisper-large-v3-turbo");         // any Groq model
stt.setLanguage("en");                          // "en", "bn", "hi", ... or "" for auto
stt.setTranslate(true);                         // use /audio/translations; auto-model = whisper-large-v3
stt.setResponseFormat(STT_FMT_TEXT);            // STT_FMT_TEXT | STT_FMT_JSON | STT_FMT_VERBOSE_JSON
stt.setTemperature(0.8f);                       // 0 = greedy (ASR default), 1.0 = max entropy
stt.setPrompt("This is a technical demo.");     // spelling/terminology hint
```

### Audio chain

```cpp
stt.setGain(8);                                 // 1..16 (default 8)
stt.setHighpassHz(120);                         // rumble filter
stt.setSilenceThreshold(300);                   // raw peak below this = silence
stt.setSilenceAction(STT_SILENCE_REJECT);       // STT_SILENCE_REJECT | STT_SILENCE_WARN | STT_SILENCE_IGNORE
stt.setMinSeconds(0.3f);
stt.setMaxSeconds(0);                           // 0 = no cap
stt.setPins(sck, ws, sd);
stt.setButtonPin(0);                            // -1 = no button
stt.useVad(true);
stt.setVadSilenceMs(1200);
stt.useLegacyI2S(false);                        // auto-detected; force only if you know why
```

### Diagnostics

```cpp
groq_stt_err_t stt.lastError();                 // enum
const char*    stt.errorText();                 // human description
uint16_t       stt.lastHttpStatus();            // Groq's HTTP code (0 if never sent)
float          stt.lastConnectMs();             // last TLS handshake time
float          stt.lastSendMs();                // time spent streaming the audio
float          stt.lastInferMs();               // Groq server-side processing
float          stt.lastLatencyMs();             // send + infer
stt.setLogLevel(1);                             // 0 silent, 1 errors (default), 2 info, 3 debug
```

### File/buffer input

```cpp
String stt.transcribeFile(fs::FS& fs, "/test.wav");      // SD, LittleFS, SPIFFS - any fs::FS
String stt.transcribeBuffer(const int16_t* pcm, size_t samples);  // in-RAM 16-bit mono PCM
```

Files stream straight from storage — no full-utterance RAM buffer. WAVs pass
through untouched; raw PCM gets a 44-byte WAV header prepended automatically.

---

## Hardware

| Pin | Classic ESP32 (verified) | ESP32-S3 |
|---|---|---|
| I2S SCK (bit clock) | GPIO 26 | GPIO 5 |
| I2S WS  (word select) | GPIO 25 | GPIO 6 |
| I2S SD  (data)        | GPIO 22 | GPIO 4 |
| BOOT button (optional)| GPIO 0  | GPIO 0 |

INMP441 wiring: `VDD → 3V3` (NOT 5 V), `GND → GND`, `L/R → GND` (left slot),
`SCK/WS/SD` as configured above.

Override with `GroqSTT stt(SCK, WS, SD, BTN)` or `stt.setPins(...)` /
`stt.setButtonPin(...)` before `begin()`.

> ⚠ Core 2.0.x's legacy I2S driver reads the INMP441 as **silence**. Use the
> arduino-esp32 3.x core (or `stt.useLegacyI2S(false)`) for INMP441.

---

## Defaults & configuration

| Setting | Default | Override |
|---|---|---|
| Model | `whisper-large-v3-turbo` | `setModel("whisper-large-v3")` |
| Sample rate | 16000 Hz | fixed by Groq |
| Gain | 8 | `setGain(8)` |
| High-pass filter | 120 Hz | `setHighpassHz(120)` |
| Soft clip | off | `useSoftClip(true)` |
| Min recording | 0.3 s | `setMinSeconds(0.3f)` |
| Max recording | none (button / VAD / fixed ms) | `setMaxSeconds(s)`; 0 = off |
| Response format | `text` | `setResponseFormat(STT_FMT_VERBOSE_JSON)` |
| Temperature | 0 (greedy) | `setTemperature(0.8f)` |
| Language | auto | `setLanguage("en")` |
| Translation | off | `setTranslate(true)` |
| Prompt | none | `setPrompt("...")` |
| VAD | off | `useVad(true)` / `setVadSilenceMs(ms)` |

All of these have matching `GROQ_STT_*` compile-time defines in
`src/groq_stt_config.h` for project-level pinning.

### Verbose JSON

`STT_FMT_VERBOSE_JSON` returns the full text plus a `segments` array with
`start` / `end` timestamps:

```json
{
  "task": "translate",
  "language": "bn",
  "text": "Hello, my name is Raihan.",
  "segments": [
    { "start": 0.0, "end": 0.92, "text": "Hello," },
    { "start": 0.92, "end": 2.16, "text": " my name is Raihan." }
  ]
}
```

Groq rejects OpenAI-style `timestamp_granularities` and returns no word-level
timestamps on its translate endpoint, so segment granularity is the finest
available there. `examples/Translate` shows the full parse.

---

## How it fits in your project

```
   +--------------------+        +---------------+        +-----------------+
   |  your project code |  <-->  |   GroqSTT     |  <-->  |   api.groq.com  |
   |  (UI, web, MQTT...) |       |  mic + upload |        |  Whisper ASR    |
   +--------------------+        +---------------+        +-----------------+
                                       ^
                                       |
                                  only owns:
                                - I2S microphone
                                - one TLS socket
                                - the WAV upload
```

The library **never** touches your Wi-Fi, your other sockets, your displays,
your MQTT client, your LVGL task, your web server. Pick the blocking
`listen()` for a one-button project or the `startRecording() / tick()` state
machine for a UI with a screen and a few buttons. The two are interchangeable
on the same instance.

For a voice assistant pipeline:

```
   mic -> GroqSTT -> text -> your server -> reply -> your TTS -> speaker
                    \_____________  this library  ____________/
```

---

## Errors

`stt.lastError()` returns one of:

| Code | Name | When |
|---|---|---|
| `STT_OK` | — | success |
| `STT_ERR_NO_WIFI` | no Wi-Fi | call `begin()` after Wi-Fi is up, or use the 3-arg `begin()` |
| `STT_ERR_NO_KEY` | no API key | `setApiKey()` or pass key to `begin()` |
| `STT_ERR_TLS_CONNECT` | TLS failed | bad network or firewall |
| `STT_ERR_UPLOAD_ABORT` | upload died | dropped socket mid-stream (server reset, Wi-Fi blip) |
| `STT_ERR_RESPONSE` | bad reply | HTTP non-200, malformed body, empty 200 |
| `STT_ERR_BUSY` | state machine busy | call `listen()` again only after `isDone()` |
| `STT_ERR_I2S_INIT` | mic init failed | check pins / wiring; S3 may need `useLegacyI2S(true)` with PSRAM |
| `STT_ERR_FILE` | file open/read failed | check path / fs::FS |
| `STT_ERR_TOO_SHORT` | recording too short | held the button for < 0.3 s; ignore or `setMinSeconds(0)` |
| `STT_ERR_SILENCE` | peak below silence threshold | turn on the room sound, or `setSilenceAction(STT_SILENCE_IGNORE)` |

---

## Performance

Typical end-to-end latency on a classic ESP32 + INMP441 + 802.11n Wi-Fi:

```
   hold   release       reply lands
    |       |              |
    +-------+--------------+-----> t
    ~1-3 s  upload      ~200-400 ms infer
```

Groq's Whisper is fast; the ceiling is usually your network upload of the
audio bytes, not the model.

---

## Tested on

- arduino-esp32 **3.0.x** and **3.2.x** (classic ESP32 + ESP32-S3)
- arduino-esp32 **2.0.14** (legacy I2S, classic ESP32)

For INMP441, **use core 3.x**. The legacy driver in core 2.x reads the INMP441
as silence.

---

## License

MIT — see [`LICENSE`](LICENSE). (c) 2026 Sk Raihan / SKR Electronics Lab.

## Contributing

Issues and PRs welcome at <https://github.com/skr-electronics-lab/groq-stt>.
The library was built with a small set of guard rails — every public method
keeps the state machine in a known state, every network call is bounded by a
timeout, and the example sketches are short enough to read in one sitting.
Please keep new code in the same spirit.
