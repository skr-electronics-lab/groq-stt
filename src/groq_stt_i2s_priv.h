#pragma once
#include <stddef.h>
#include <stdint.h>

// Private cross-TU interface between the two I2S backends and the mic
// plumbing in groq_stt_i2s.cpp. Compiled as separate .cpp files on purpose:
// <driver/i2s.h> (legacy) and <ESP_I2S.h> (core 3.x) both typedef i2s_mode_t
// and cannot share one translation unit.

// Backend A: ESP_I2S (arduino-esp32 core 3.x). Only compiled there; the
// plumbing never references it on core 2.x.
bool i2sBackendNewBegin(int sck, int ws, int sd, uint32_t rate);
bool i2sBackendNewActive();
size_t i2sBackendNewRead(int16_t* buf, size_t maxSamples);
void i2sBackendNewStop();
void i2sBackendNewZero();

// Backend B: legacy driver/i2s.h. Compiled on every core.
bool i2sBackendLegacyBegin(int sck, int ws, int sd, uint32_t rate);
size_t i2sBackendLegacyRead(int16_t* buf, size_t maxSamples);
void i2sBackendLegacyStop();
void i2sBackendLegacyZero();
