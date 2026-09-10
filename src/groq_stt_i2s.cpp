#include "groq_stt.h"
#include "groq_stt_i2s_priv.h"
#include "groq_stt_config.h"

// Shared 1 KB scratch for one upload chunk (int16 PCM).
// Deliberately not heap: reused across all recordings.
static int16_t s_chunkBuf[GROQ_STT_CHUNK_SAMPLES];

uint8_t* GroqSTT::micChunkBuf() { return (uint8_t*)s_chunkBuf; }

bool GroqSTT::micBegin() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  // Core 3.x: ESP_I2S is the only driver (the legacy driver conflicts at boot).
  if (!_forceLegacy && i2sBackendNewBegin(_sck, _ws, _sd, GROQ_STT_SAMPLE_RATE)) {
    log(2, "mic: ESP_I2S backend (core 3.x)");
    return true;
  }
  log(1, "mic: ESP_I2S init failed - check SCK=%d WS=%d SD=%d wiring", _sck, _ws, _sd);
  return false;
#else
  if (i2sBackendLegacyBegin(_sck, _ws, _sd, GROQ_STT_SAMPLE_RATE)) {
    log(2, "mic: legacy driver backend (core 2.x)");
    return true;
  }
  log(1, "mic: legacy init failed - check SCK=%d WS=%d SD=%d wiring", _sck, _ws, _sd);
  return false;
#endif
}

size_t GroqSTT::micRead(int16_t* buf, size_t maxSamples) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  return i2sBackendNewRead(buf, maxSamples);
#else
  return i2sBackendLegacyRead(buf, maxSamples);
#endif
}

void GroqSTT::micStop() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  i2sBackendNewStop();
#else
  i2sBackendLegacyStop();
#endif
}

void GroqSTT::micZero() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  i2sBackendNewZero(); // DMA is fresh after begin()
#else
  i2sBackendLegacyZero();
#endif
}
