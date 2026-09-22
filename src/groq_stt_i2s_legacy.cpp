// Backend B: legacy driver/i2s.h. Compiled ONLY on core 2.x (<3). On core 3.x
// the legacy driver conflicts with the new ESP_I2S driver at runtime (boot-time
// abort), so core 3 builds ship stubs and always use the ESP_I2S backend.
#include <Arduino.h> // defines ESP_ARDUINO_VERSION_MAJOR on every core
#include "groq_stt_i2s_priv.h"
#include "groq_stt_config.h"

#if ESP_ARDUINO_VERSION_MAJOR < 3

#include <driver/i2s.h>

static const int s_legacyPort = 0; // I2S_NUM_0

bool i2sBackendLegacyBegin(int sck, int ws, int sd, uint32_t rate) {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = rate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S);
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 1024;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = false;
  cfg.fixed_mclk = 0;

  if (i2s_driver_install((i2s_port_t)s_legacyPort, &cfg, 0, NULL) != ESP_OK) {
    return false;
  }
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = sck;
  pins.ws_io_num = ws;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = sd;
  if (i2s_set_pin((i2s_port_t)s_legacyPort, &pins) != ESP_OK) {
    i2s_driver_uninstall((i2s_port_t)s_legacyPort);
    return false;
  }
  i2s_zero_dma_buffer((i2s_port_t)s_legacyPort);
  return true;
}

size_t i2sBackendLegacyRead(int16_t* buf, size_t maxSamples) {
  // INMP441: 24-bit data left-aligned in a 32-bit slot -> >>8 lands a 16-bit sample.
  int32_t raw[GROQ_STT_CHUNK_SAMPLES];
  size_t bytes = 0;
  if (i2s_read((i2s_port_t)s_legacyPort, raw, maxSamples * sizeof(int32_t), &bytes,
               pdMS_TO_TICKS(100)) != ESP_OK) {
    return 0;
  }
  size_t n = bytes / sizeof(int32_t);
  if (n > maxSamples) n = maxSamples;
  for (size_t i = 0; i < n; i++) buf[i] = (int16_t)(raw[i] >> 8);
  return n;
}

void i2sBackendLegacyStop() {
  i2s_driver_uninstall((i2s_port_t)s_legacyPort);
}

void i2sBackendLegacyZero() {
  i2s_zero_dma_buffer((i2s_port_t)s_legacyPort);
}

#else // core 3.x stubs - never called

bool i2sBackendLegacyBegin(int sck, int ws, int sd, uint32_t rate) { return false; }
size_t i2sBackendLegacyRead(int16_t* buf, size_t maxSamples) { return 0; }
void i2sBackendLegacyStop() {}
void i2sBackendLegacyZero() {}

#endif
