#pragma once
#include <Arduino.h>
#include <fs.h>
#include <WiFiClientSecure.h>
#include "groq_stt_config.h"

// ---- errors ----
typedef enum {
  STT_OK = 0,
  STT_ERR_NO_WIFI,
  STT_ERR_NO_KEY,
  STT_ERR_TLS_CONNECT,
  STT_ERR_I2S_INIT,
  STT_ERR_I2S_READ,
  STT_ERR_TOO_SHORT,
  STT_ERR_SILENCE,
  STT_ERR_UPLOAD_ABORT,
  STT_ERR_HTTP,
  STT_ERR_RESPONSE,
  STT_ERR_FILE,
  STT_ERR_BUSY
} groq_stt_err_t;

const char* groq_stt_errorText(groq_stt_err_t e);

typedef enum { STT_FMT_TEXT = 0, STT_FMT_JSON, STT_FMT_VERBOSE_JSON } groq_stt_fmt_t;
typedef enum { STT_SILENCE_REJECT = 0, STT_SILENCE_WARN, STT_SILENCE_IGNORE } groq_stt_silence_t;

typedef void (*groq_stt_done_cb)(const String& text);
typedef void (*groq_stt_err_cb)(groq_stt_err_t err, const String& detail);
typedef void (*groq_stt_level_cb)(float rms01);

class GroqSTT {
public:
  GroqSTT();                                  // per-chip default pins + BOOT button
  GroqSTT(int sck, int ws, int sd);           // custom mic pins
  GroqSTT(int sck, int ws, int sd, int btnPin); // custom mic + button

  // ---- lifecycle -----------------------------------------------------
  bool begin(const char* apiKey);                    // Wi-Fi managed by user
  bool begin(const char* ssid, const char* pass, const char* apiKey);
  void end();

  // ---- blocking convenience -----------------------------------------
  String listen();               // button-held; buttonless: VAD silence gap or safety cap
  String listen(uint32_t ms);    // fixed-duration recording

  // ---- non-blocking ---------------------------------------------------
  bool startRecording();
  bool tick();                   // true while machine is active
  bool isRecording() { return _state == STT_STATE_RECORDING; }
  bool isBusy() {
    return _state == STT_STATE_PREP || _state == STT_STATE_RECORDING ||
           _state == STT_STATE_FINISHING || _state == STT_STATE_READING;
  }
  bool isDone() { return _state == STT_STATE_DONE || _state == STT_STATE_ERROR; }
  String getResult() { return _result; }
  void stopRecording();

  void onDone(groq_stt_done_cb cb) { _onDone = cb; }
  void onError(groq_stt_err_cb cb) { _onError = cb; }
  void onLevel(groq_stt_level_cb cb) { _onLevel = cb; }

  // ---- audio sources ---------------------------------------------------
  String transcribeFile(fs::FS& fs, const char* path);        // SD / LittleFS / SPIFFS
  String transcribeBuffer(const int16_t* pcm, size_t samples);

  // ---- request customization ------------------------------------------
  void setApiKey(const char* k) { _apiKey = k; }
  void setModel(const char* m) { _model = m; }
  void setLanguage(const char* l) { _language = l; }          // "" = auto-detect
  void setTranslate(bool on) { _translate = on; } // Groq /audio/translations; auto-switches model to whisper-large-v3
  void setResponseFormat(groq_stt_fmt_t f) { _format = f; }
  void setPrompt(const char* p) { _prompt = p; }
  void setTemperature(float t) { _temperature = t; }          // 0 = greedy (ASR default)

  // ---- DSP customization ----------------------------------------------
  void setGain(uint8_t g) { _gain = g ? g : 1; }
  void setHighpassHz(uint16_t hz);
  void useSoftClip(bool on) { _softClip = on; }
  void setSilenceThreshold(uint16_t peak) { _silencePeak = peak; }
  void setSilenceAction(groq_stt_silence_t a) { _silenceAction = a; }
  void setMinSeconds(float s) { _minSeconds = s; }
  void setMaxSeconds(float s) { _maxSeconds = s; }            // 0 = no cap

  // ---- recording behaviour --------------------------------------------
  // Voice activity detection: auto-stops a recording after the signal stays
  // below the tracked noise floor for VAD_TAIL_MS. Handy when no button is
  // wired; the default button-driven flow does not need it.
  void useVad(bool on) { _vad = on; }
  void setVadSilenceMs(uint32_t ms) { _vadTailMs = ms ? ms : GROQ_STT_VAD_TAIL_MS; }

  // ---- plumbing / diagnostics ------------------------------------------
  void useLegacyI2S(bool legacy) { _forceLegacy = legacy; }
  void setCACert(const char* cert);
  void setPins(int sck, int ws, int sd) { _sck = sck; _ws = ws; _sd = sd; }
  void setButtonPin(int pin) { _btnPin = pin; }
  bool isWifiConnected();
  groq_stt_err_t lastError() const { return _lastError; }
  const char* errorText() const { return groq_stt_errorText(_lastError); }
  uint16_t lastHttpStatus() const { return _httpStatus; }
  // timing of the last transcription (milliseconds):
  float lastConnectMs() const { return _connectMs; }   // TLS handshake of the last connect
  float lastSendMs()    const { return _sendMs; }      // time to stream the audio to Groq
  float lastInferMs()   const { return _inferMs; }     // Groq server-side processing
  float lastLatencyMs() const { return _latencyMs; }   // send + infer (kept for back-compat)
  void setLogLevel(uint8_t level) { _logLevel = level; }  // 0 silent, 1 errors (default), 2 info, 3 debug

private:
  // state machine steps
  bool stepPrep();
  bool stepRecording();
  bool stepFinishing();
  bool stepReading();
  void finishDone(const String& text);
  void finishError(groq_stt_err_t err, const String& detail);
  bool handleReplyDone();      // map parsed reply -> result/error (used by all paths)
  String settleReply();        // blocking wait on reply then return result

  // DSP
  int16_t applyDsp(int16_t s);
  void resetFilters();
  void computeHpCoef();
  void trackVad(float rms01);

  // ---- I2S backend (groq_stt_i2s.cpp) ---------------------------------
  bool micBegin();
  size_t micRead(int16_t* buf, size_t maxSamples);
  void micStop();
  void micZero();
  uint8_t* micChunkBuf();   // shared 1 KB scratch

  // ---- HTTP backend (groq_stt_http.cpp) --------------------------------
  bool httpEnsureConnected();
  bool httpSocketAlive();    // probe-safe (never touches fd 0 after stop())
  void httpClose();          // teardown after each reply (server sends Connection: close)
  bool httpStartRequest(uint32_t wavDataLen);  // 0xFFFFFFFF = streamed (unknown len);
                                               // 0 = file carries its own full bytes
  bool httpSendAudioChunk(const uint8_t* data, size_t len);
  bool httpFinishUpload();
  int  httpReadPoll();      // 0 reading, 1 done, -1 error
  void httpAbort();

  // helpers used by all impl files
  void setError(groq_stt_err_t err, const String& detail);
  bool checkPrereqs();
  void log(uint8_t level, const char* fmt, ...);

  // ---- pins / config ----
  int _sck, _ws, _sd, _btnPin;
  String _apiKey, _model, _language, _prompt;
  bool _translate;
  groq_stt_fmt_t _format;
  float _temperature;
  uint8_t _gain;
  uint16_t _hpHz;
  bool _softClip;
  uint16_t _silencePeak;
  groq_stt_silence_t _silenceAction;
  float _minSeconds, _maxSeconds;
  bool _vad;
  uint32_t _vadTailMs;
  bool _forceLegacy;
  const char* _caCert;
  uint8_t _logLevel;

  // ---- state / results ----
  uint8_t _state;            // groq_stt_state_t stored as uint8 to keep header lean
  groq_stt_err_t _lastError;
  String _result, _errorDetail;
  uint16_t _httpStatus;
  float _latencyMs, _latencyStart;   // _latencyMs = send + infer (back-compat)
  float _sendMs;                     // audio-stream time (mic -> finishUpload)
  float _inferMs;                    // Groq inference time (finishUpload -> response)
  float _connectMs;                  // last TLS handshake time
  bool _micRunning;
  bool _fixedDuration;
  uint32_t _fixedMs;
  uint32_t _recStartMs, _lastSampleMs;
  uint32_t _totalSamples;
  int16_t _clipPeak;
  float _rmsAcc;

  // VAD tracking state
  float _vadNoise;        // adaptive ambient noise floor (RMS of one chunk)
  bool _vadSpeech;        // currently in a speech segment
  uint32_t _vadQuietMs;   // ms of trailing silence since last speech energy

  // DSP filter state
  float _hpY1, _hpX1, _hpY2, _hpX2, _hpCoef;

  // callbacks
  groq_stt_done_cb _onDone;
  groq_stt_err_cb _onError;
  groq_stt_level_cb _onLevel;

  // HTTP transport state
  WiFiClientSecure _client;
  bool _httpOpen;   // we hold an open TLS socket (core 3.x leaves fd=0 after
                    // stop(), so connected() must not be probed when closed)
  uint8_t _rxState;   // groq_stt_rxstate_t
  uint16_t _rxStatus;
  bool _rxChunked;
  bool _rxNoBody;     // e.g. Connection: close with no content-length
  size_t _rxRemain;
  uint32_t _rxDeadline;
  String _rxLine, _rxBody;
};
