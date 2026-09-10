// Backend A: ESP_I2S (arduino-esp32 core 3.x).
//   32-bit mono slot + I2S_RX_TRANSFORM_32_TO_16 -> readBytes() hands back
//   int16 PCM directly. Compiled only on core 3.x.
#include "groq_stt_i2s_priv.h"
#include "groq_stt_config.h"
#include <Arduino.h> // defines ESP_ARDUINO_VERSION_MAJOR on every core

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  #include <ESP_I2S.h>

  static I2SClass s_i2sNew;
  static bool s_i2sNewBusy = false;

  bool i2sBackendNewBegin(int sck, int ws, int sd, uint32_t rate) {
    s_i2sNew.setPins(sck, ws, -1, sd);               // dout unused on RX
    if (!s_i2sNew.begin(I2S_MODE_STD, rate, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO)) {
      return false;
    }
    // Reconfigure RX with the 32->16 transform: readBytes() hands back int16.
    if (!s_i2sNew.configureRX(rate, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO, I2S_RX_TRANSFORM_32_TO_16)) {
      return false;
    }
    s_i2sNewBusy = true;
    return true;
  }

  bool i2sBackendNewActive() { return s_i2sNewBusy; }

  size_t i2sBackendNewRead(int16_t* buf, size_t maxSamples) {
    int got = s_i2sNew.readBytes((char*)buf, (int)(maxSamples * sizeof(int16_t)));
    if (got < 0) return 0;
    return (size_t)got / sizeof(int16_t);
  }

  void i2sBackendNewStop() {
    if (s_i2sNewBusy) {
      s_i2sNew.end();
      s_i2sNewBusy = false;
    }
  }

  void i2sBackendNewZero() {} // DMA is fresh after begin()
#endif
