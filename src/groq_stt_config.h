#pragma once
// groq_stt configuration defaults. All overridable via -D or the public API.

// ---- pins: ESP32-S3 (INMP441) ----
#ifndef GROQ_STT_S3_SCK
#define GROQ_STT_S3_SCK 5
#endif
#ifndef GROQ_STT_S3_WS
#define GROQ_STT_S3_WS 6
#endif
#ifndef GROQ_STT_S3_SD
#define GROQ_STT_S3_SD 4
#endif

// ---- pins: classic ESP32 (GPIO 6-11 = flash, 12 = strapping: avoided) ----
// SCK 26 / WS 25 / SD 22 verified against the user's working INMP441 rig.
// Core must be 3.x: this mic needs the ESP_I2S driver + 32-to-16 transform
// (the legacy core-2 I2S driver reads it as silence - see reference rig).
#ifndef GROQ_STT_ESP32_SCK
#define GROQ_STT_ESP32_SCK 26
#endif
#ifndef GROQ_STT_ESP32_WS
#define GROQ_STT_ESP32_WS 25
#endif
#ifndef GROQ_STT_ESP32_SD
#define GROQ_STT_ESP32_SD 22
#endif

#ifndef GROQ_STT_BUTTON_PIN
#define GROQ_STT_BUTTON_PIN 0 // BOOT, active-LOW
#endif

// ---- audio ----
#ifndef GROQ_STT_SAMPLE_RATE
#define GROQ_STT_SAMPLE_RATE 16000
#endif
#ifndef GROQ_STT_GAIN
#define GROQ_STT_GAIN 8
#endif
#ifndef GROQ_STT_HIGHPASS_HZ
#define GROQ_STT_HIGHPASS_HZ 120 // dual cascaded one-pole; the rumble fix
#endif
#ifndef GROQ_STT_SILENCE_PEAK
#define GROQ_STT_SILENCE_PEAK 300 // peak below this on a clip => silence (Whisper hallucinates on silence)
#endif
#ifndef GROQ_STT_MIN_SECONDS
#define GROQ_STT_MIN_SECONDS 0.3f
#endif
// Maximum recording length. 0 = unlimited: the recording ends when the active
// stop source fires (button release, VAD silence gap, or an explicit listen(ms)).
// This value is only consulted as a fallback when a recording has no stop
// source at all (buttonless + VAD off + no fixed duration), so the library is
// never left recording forever.
#ifndef GROQ_STT_MAX_SECONDS
#define GROQ_STT_MAX_SECONDS 15.0f
#endif

// ---- voice activity detection (opt-in via useVad(true)) ----
// VAD lets a buttonless recording stop itself: it tracks the ambient noise
// floor and stops after the signal has stayed below the floor for VAD_TAIL_MS.
// The defaults are relative to the measured noise floor, not absolute, so the
// INMP441's high gain-8 noise floor does not defeat it.
#ifndef GROQ_STT_VAD_TAIL_MS
#define GROQ_STT_VAD_TAIL_MS 1200 // trailing silence before auto-stop
#endif
#ifndef GROQ_STT_VAD_SPEECH_RATIO
#define GROQ_STT_VAD_SPEECH_RATIO 2.0f // RMS must exceed noise floor x this to count as speech
#endif
#ifndef GROQ_STT_VAD_NOISE_RATIO
#define GROQ_STT_VAD_NOISE_RATIO 1.5f // RMS below noise floor x this counts as silence
#endif

// ---- request ----
#ifndef GROQ_STT_MODEL
#define GROQ_STT_MODEL "whisper-large-v3-turbo"
#endif
#ifndef GROQ_STT_LANGUAGE
#define GROQ_STT_LANGUAGE "en"
#endif
#ifndef GROQ_STT_HOST
#define GROQ_STT_HOST "api.groq.com"
#endif
#ifndef GROQ_STT_PORT
#define GROQ_STT_PORT 443
#endif
#ifndef GROQ_STT_PATH
#define GROQ_STT_PATH "/openai/v1/audio/transcriptions"
#endif
// Groq's translation endpoint is separate and only whisper-large-v3 translates
// (whisper-large-v3-turbo and whisper-small do NOT). The library switches path
// and model automatically when setTranslate(true) is used.
#ifndef GROQ_STT_PATH_TRANSLATE
#define GROQ_STT_PATH_TRANSLATE "/openai/v1/audio/translations"
#endif
#ifndef GROQ_STT_TRANSLATE_MODEL
#define GROQ_STT_TRANSLATE_MODEL "whisper-large-v3"
#endif
// Whisper sampling temperature; 0 = greedy (the recommended default for ASR).
#ifndef GROQ_STT_TEMPERATURE
#define GROQ_STT_TEMPERATURE 0.0f
#endif
#ifndef GROQ_STT_AGENT
#define GROQ_STT_AGENT "groq_stt/1.0.0" // Groq rejects requests without User-Agent
#endif

// ---- network timeouts (ms) ----
#ifndef GROQ_STT_CONNECT_TIMEOUT
#define GROQ_STT_CONNECT_TIMEOUT 10000
#endif
#ifndef GROQ_STT_READ_TIMEOUT
#define GROQ_STT_READ_TIMEOUT 20000
#endif
#ifndef GROQ_STT_PREWARM_INTERVAL
#define GROQ_STT_PREWARM_INTERVAL 2000 // min ms between prewarm attempts while idle
#endif

// ---- internals ----
#ifndef GROQ_STT_CHUNK_SAMPLES
#define GROQ_STT_CHUNK_SAMPLES 512 // 1 KB int16 per upload chunk
#endif

// state machine states (shared across groq_stt.cpp / groq_stt_http.cpp)
#define STT_STATE_IDLE 0
#define STT_STATE_PREP 1
#define STT_STATE_RECORDING 2
#define STT_STATE_FINISHING 3
#define STT_STATE_READING 4
#define STT_STATE_DONE 5
#define STT_STATE_ERROR 6

// reply-parser sub-states (groq_stt_http.cpp)
#define GROQ_STT_RX_STATUS 0
#define GROQ_STT_RX_HEADERS 1
#define GROQ_STT_RX_CHUNK_SIZE 2
#define GROQ_STT_RX_CHUNK_DATA 3
#define GROQ_STT_RX_CHUNK_CRLF 4
#define GROQ_STT_RX_CHUNK_LF 5
#define GROQ_STT_RX_TRAILER 6
#define GROQ_STT_RX_RAW 7
