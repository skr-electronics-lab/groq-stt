#include "groq_stt.h"
#include "groq_stt_config.h"
#include <WiFi.h>
#include <stdarg.h>

const char* groq_stt_errorText(groq_stt_err_t e) {
  switch (e) {
    case STT_OK: return "OK";
    case STT_ERR_NO_WIFI: return "Wi-Fi not connected";
    case STT_ERR_NO_KEY: return "empty API key";
    case STT_ERR_TLS_CONNECT: return "TLS/socket connect failed";
    case STT_ERR_I2S_INIT: return "mic init failed";
    case STT_ERR_I2S_READ: return "mic read failed";
    case STT_ERR_TOO_SHORT: return "recording too short";
    case STT_ERR_SILENCE: return "silence detected";
    case STT_ERR_UPLOAD_ABORT: return "upload aborted";
    case STT_ERR_HTTP: return "HTTP error";
    case STT_ERR_RESPONSE: return "malformed reply";
    case STT_ERR_FILE: return "file error";
    case STT_ERR_BUSY: return "busy";
  }
  return "unknown";
}

GroqSTT::GroqSTT()
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    : _sck(GROQ_STT_S3_SCK), _ws(GROQ_STT_S3_WS), _sd(GROQ_STT_S3_SD), _btnPin(GROQ_STT_BUTTON_PIN)
#else
    : _sck(GROQ_STT_ESP32_SCK), _ws(GROQ_STT_ESP32_WS), _sd(GROQ_STT_ESP32_SD), _btnPin(GROQ_STT_BUTTON_PIN)
#endif
{
  _model = GROQ_STT_MODEL;
  _language = GROQ_STT_LANGUAGE;
  _prompt = "";
  _translate = false;
  _format = STT_FMT_TEXT;
  _temperature = GROQ_STT_TEMPERATURE;
  _gain = GROQ_STT_GAIN;
  _hpHz = GROQ_STT_HIGHPASS_HZ;
  _softClip = false;
  _silencePeak = GROQ_STT_SILENCE_PEAK;
  _silenceAction = STT_SILENCE_REJECT;
  _minSeconds = GROQ_STT_MIN_SECONDS;
  _maxSeconds = 0; // no cap by default; recording ends on button release / VAD / fixed ms
  _vad = false;
  _vadTailMs = GROQ_STT_VAD_TAIL_MS;
  _vadNoise = 0;
  _vadSpeech = false;
  _vadQuietMs = 0;
  _forceLegacy = false;
  _caCert = nullptr;
  _logLevel = 1; // errors + weak-signal warnings by default; setLogLevel(2/3) for info/debug
  _state = STT_STATE_IDLE;
  _lastError = STT_OK;
  _httpStatus = 0;
  _latencyMs = 0;
  _latencyStart = 0;
  _micRunning = false;
  _fixedDuration = false;
  _fixedMs = 0;
  _recStartMs = 0;
  _lastSampleMs = 0;
  _totalSamples = 0;
  _clipPeak = 0;
  _rmsAcc = 0;
  _btnHeldAtStart = false;
  _hpY1 = _hpX1 = _hpY2 = _hpX2 = 0;
  _hpCoef = 0;
  _onDone = nullptr;
  _onError = nullptr;
  _onLevel = nullptr;
  _rxState = GROQ_STT_RX_STATUS;
  _rxStatus = 0;
  _rxChunked = false;
  _rxNoBody = false;
  _rxRemain = 0;
  _rxDeadline = 0;
  if (_btnPin >= 0) pinMode(_btnPin, INPUT_PULLUP);
  computeHpCoef();
}

GroqSTT::GroqSTT(int sck, int ws, int sd) : GroqSTT() { _sck = sck; _ws = ws; _sd = sd; }
GroqSTT::GroqSTT(int sck, int ws, int sd, int btnPin) : GroqSTT() {
  _sck = sck; _ws = ws; _sd = sd; _btnPin = btnPin;
  if (_btnPin >= 0) pinMode(_btnPin, INPUT_PULLUP);
}

void GroqSTT::setHighpassHz(uint16_t hz) { _hpHz = hz; computeHpCoef(); }
void GroqSTT::computeHpCoef() {
  if (_hpHz == 0) { _hpCoef = 0; return; }
  float dt = 1.0f / (float)GROQ_STT_SAMPLE_RATE;
  float rc = 1.0f / (2.0f * 3.14159265358979f * (float)_hpHz);
  _hpCoef = rc / (rc + dt);
}
void GroqSTT::resetFilters() { _hpY1 = _hpX1 = _hpY2 = _hpX2 = 0; }

void GroqSTT::setCACert(const char* cert) { _caCert = cert; }

bool GroqSTT::isWifiConnected() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  return WiFi.isConnected();
#else
  return WiFi.status() == WL_CONNECTED;
#endif
}

void GroqSTT::log(uint8_t level, const char* fmt, ...) {
  if (_logLevel < level) return;
  char buf[192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print("[groq_stt] ");
  Serial.println(buf);
}

void GroqSTT::setError(groq_stt_err_t err, const String& detail) {
  _lastError = err;
  _errorDetail = detail;
}

void GroqSTT::finishDone(const String& text) {
  _result = text;
  _latencyMs = (float)(millis() - _latencyStart);
  _state = STT_STATE_DONE;
  log(2, "done in %.0f ms, %u chars", _latencyMs, (unsigned)text.length());
  if (_onDone) _onDone(text);
}

void GroqSTT::finishError(groq_stt_err_t err, const String& detail) {
  setError(err, detail);
  _result = "";
  _latencyMs = (float)(millis() - _latencyStart);
  _state = STT_STATE_ERROR;
  log(1, "error %d (%s)%s%s", (int)err, groq_stt_errorText(err),
      detail.length() ? ": " : "", detail.c_str());
  if (_onError) _onError(err, detail);
}

bool GroqSTT::checkPrereqs() {
  if (!isWifiConnected()) { finishError(STT_ERR_NO_WIFI, "Wi-Fi not connected"); return false; }
  if (_apiKey.length() == 0) { finishError(STT_ERR_NO_KEY, "set an API key via begin()/setApiKey()"); return false; }
  return true;
}

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------
bool GroqSTT::begin(const char* apiKey) {
  _apiKey = apiKey ? apiKey : "";
  if (!isWifiConnected()) {
    setError(STT_ERR_NO_WIFI, "Wi-Fi not connected (connect first, or use begin(ssid,pass,key))");
    log(1, "begin: %s", _errorDetail.c_str());
    return false;
  }
  if (_btnPin >= 0) pinMode(_btnPin, INPUT_PULLUP);
  log(2, "ready - model %s, language '%s', format %s",
      _model.c_str(), _language.length() ? _language.c_str() : "auto",
      _format == STT_FMT_TEXT ? "text" : (_format == STT_FMT_JSON ? "json" : "verbose_json"));
  return true;
}

bool GroqSTT::begin(const char* ssid, const char* pass, const char* apiKey) {
  log(2, "connecting Wi-Fi '%s' ...", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  uint32_t t0 = millis();
  while (!isWifiConnected() && millis() - t0 < 20000) {
    delay(250);
    if (_logLevel >= 2) Serial.print(".");
  }
  if (_logLevel >= 2) Serial.println();
  if (!isWifiConnected()) {
    setError(STT_ERR_NO_WIFI, "connect to AP timed out");
    log(1, "begin: %s", _errorDetail.c_str());
    return false;
  }
  log(2, "connected, IP %s", WiFi.localIP().toString().c_str());
  return begin(apiKey);
}

void GroqSTT::end() {
  micStop();
  _micRunning = false;
  _client.stop();
  _state = STT_STATE_IDLE;
}

// ---------------------------------------------------------------------------
// non-blocking: startRecording / tick / stopRecording
// ---------------------------------------------------------------------------
bool GroqSTT::startRecording() {
  if (isBusy()) {
    finishError(STT_ERR_BUSY, "state machine active — wait for isDone()");
    return false;
  }
  if (_state == STT_STATE_DONE || _state == STT_STATE_ERROR) _state = STT_STATE_IDLE; // consume previous result
  _lastError = STT_OK;
  _result = "";
  _errorDetail = "";
  _httpStatus = 0;
  _latencyMs = 0;
  _sendMs = 0;
  _inferMs = 0;
  _clipPeak = 0;
  _totalSamples = 0;
  _rmsAcc = 0;
  _vadNoise = 0;
  _vadSpeech = false;
  _vadQuietMs = 0;
  _btnHeldAtStart = (_btnPin >= 0 && digitalRead(_btnPin) == LOW);

  if (!checkPrereqs()) return false;

  if (!_micRunning) {
    if (!micBegin()) {
      String hint;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
      hint = "if PSRAM is enabled on ESP32-S3, ESP_I2S GDMA conflicts — call useLegacyI2S(true)";
#else
      hint = "check INMP441 wiring (VDD=3V3, GND, SCK/WS/SD)";
#endif
      finishError(STT_ERR_I2S_INIT, hint);
      return false;
    }
    _micRunning = true;
  }
  micZero();

  resetFilters();
  _recStartMs = millis();
  _lastSampleMs = millis();
  _state = STT_STATE_PREP;
  return true;
}

void GroqSTT::stopRecording() {
  if (_state == STT_STATE_RECORDING) _state = STT_STATE_FINISHING;
  else if (_state == STT_STATE_PREP) _state = STT_STATE_IDLE;
}

bool GroqSTT::tick() {
  switch (_state) {
    case STT_STATE_IDLE: return false;
    case STT_STATE_PREP: return stepPrep();
    case STT_STATE_RECORDING: return stepRecording();
    case STT_STATE_FINISHING: return stepFinishing();
    case STT_STATE_READING: return stepReading();
    default: return false; // DONE / ERROR
  }
}

// ---------------------------------------------------------------------------
// state machine steps
// ---------------------------------------------------------------------------
bool GroqSTT::stepPrep() {
  if (!httpSocketAlive()) {
    log(2, "connecting TLS to %s ...", GROQ_STT_HOST);
    if (!httpEnsureConnected()) {
      finishError(STT_ERR_TLS_CONNECT, "could not reach " GROQ_STT_HOST);
      return false;
    }
    log(2, "TLS up");
  }
  if (!httpStartRequest(0xFFFFFFFF)) {  // unknown WAV lengths: streaming
    finishError(STT_ERR_TLS_CONNECT, "failed to send request head");
    return false;
  }
  _state = STT_STATE_RECORDING;
  _latencyStart = millis();
  return true;
}

bool GroqSTT::stepRecording() {
  // record+upload a chunk
  int16_t* chunk = (int16_t*)micChunkBuf();
  size_t n = micRead(chunk, GROQ_STT_CHUNK_SAMPLES);
  if (n) {
    for (size_t i = 0; i < n; i++) {
      chunk[i] = applyDsp(chunk[i]);
      int32_t a = chunk[i] < 0 ? -chunk[i] : chunk[i];
      if (a > _clipPeak) _clipPeak = (int16_t)(a > 32767 ? 32767 : a);
      _rmsAcc += ((float)chunk[i] * chunk[i]) / 32768.0f / 32768.0f;
    }
    _totalSamples += n;
    _lastSampleMs = millis();
    if (!httpSendAudioChunk((const uint8_t*)chunk, n * sizeof(int16_t))) {
      finishError(STT_ERR_UPLOAD_ABORT, "socket died mid-upload");
      return false;
    }
    float meanSq = _rmsAcc / (float)n;
    if (_onLevel) _onLevel(meanSq);
    _rmsAcc = 0;
    if (_vad) trackVad(sqrtf(meanSq));
  } else if (!httpSocketAlive()) {
    finishError(STT_ERR_UPLOAD_ABORT, "connection dropped");
    return false;
  }

  // decide whether to stop.
  //  - fixed listen(ms) timer
  //  - button release (wired default)
  //  - VAD (buttonless, opt-in): once speech has started, stop after a silence
  //    gap; if speech never starts, give up after the safety timeout so a quiet
  //    room doesn't record forever.
  //  - an explicit _maxSeconds always applies as an opt-in ceiling (0 = off).
  //    Buttonless + VAD-off recordings get the configurable GROQ_STT_MAX_SECONDS
  //    as an implicit ceiling, because nothing else can ever stop them.
  uint32_t elapsedMs = millis() - _recStartMs;
  bool stopNow = false;
  if (_fixedDuration) {
    stopNow = elapsedMs >= _fixedMs;
  } else if (_btnPin >= 0 && _btnHeldAtStart) {
    stopNow = digitalRead(_btnPin) == HIGH;               // button released (active-LOW)
  } else if (_vad) {
    if (_vadSpeech) {
      stopNow = _vadQuietMs >= _vadTailMs;
    } else {
      float lead = _maxSeconds > 0 ? _maxSeconds : GROQ_STT_MAX_SECONDS;
      stopNow = elapsedMs >= (uint32_t)(lead * 1000.0f);
    }
  }
  if (!stopNow) {
    float limit = -1.0f;
    if (_maxSeconds > 0) limit = _maxSeconds;                    // explicit ceiling
    else if ((!_btnHeldAtStart || _btnPin < 0) && !_fixedDuration && !_vad) limit = GROQ_STT_MAX_SECONDS; // implicit
    if (limit > 0 && elapsedMs >= (uint32_t)(limit * 1000.0f)) {
      log(2, "max duration reached");
      stopNow = true;
    }
  }

  if (stopNow) {
    float secs = (float)_totalSamples / GROQ_STT_SAMPLE_RATE;
    if (secs < _minSeconds) {
      httpAbort();
      String d = "only " + String(secs, 2) + " s recorded (min " + String(_minSeconds, 2) + " s)";
      finishError(STT_ERR_TOO_SHORT, d);
      return false;
    }
    if (_clipPeak < _silencePeak) {
      if (_silenceAction == STT_SILENCE_REJECT) {
        httpAbort();
        String d = "peak " + String(_clipPeak) + " < threshold " + String(_silencePeak) +
                   " — check VDD=3V3, L/R->GND, SD pin wiring";
        finishError(STT_ERR_SILENCE, d);
        return false;
      } else if (_silenceAction == STT_SILENCE_WARN) {
        log(1, "weak signal: peak %d (wiring hint: VDD=3V3, L/R->GND)", _clipPeak);
      }
    } else {
      log(3, "clip peak %d", _clipPeak);
    }
    _state = STT_STATE_FINISHING;
    return true;
  }
  return true;
}

// Adaptive voice activity detection, fed one chunk-RMS (0..1) at a time.
// Tracks the ambient noise floor asymmetrically: it follows the signal down
// quickly but only leaks up very slowly, so speech never drags the "noise"
// estimate up with it. Once speech has been detected the recorder stops after
// _vadTailMs of sustained quiet.
void GroqSTT::trackVad(float rms) {
  if (_vadNoise <= 0) { _vadNoise = rms; return; } // first chunk seeds the floor
  if (rms < _vadNoise) _vadNoise += (rms - _vadNoise) * 0.05f;
  else                 _vadNoise += (rms - _vadNoise) * 0.002f;

  bool above = rms >= _vadNoise * GROQ_STT_VAD_SPEECH_RATIO;
  bool below = rms <= _vadNoise * GROQ_STT_VAD_NOISE_RATIO;
  float chunkMs = (float)GROQ_STT_CHUNK_SAMPLES * 1000.0f / (float)GROQ_STT_SAMPLE_RATE;
  if (above) {
    _vadSpeech = true;
    _vadQuietMs = 0;
  } else if (_vadSpeech && below) {
    _vadQuietMs += (uint32_t)chunkMs;
  }
}

bool GroqSTT::stepFinishing() {
  if (!httpFinishUpload()) {
    finishError(STT_ERR_UPLOAD_ABORT, "failed to terminate upload");
    return false;
  }
  _sendMs = (float)(millis() - _latencyStart);  // request-head sent -> all bytes flushed
  log(3, "upload finished, awaiting reply (send %.0f ms)", _sendMs);
  _state = STT_STATE_READING;
  return true;
}

bool GroqSTT::stepReading() {
  int r = httpReadPoll();
  if (r < 0) {
    finishError(STT_ERR_RESPONSE, "reply timed out or truncated");
    return false;
  }
  if (r == 0) return true; // keep pumping
  return handleReplyDone();
}

// Map a fully parsed reply (_httpStatus/_rxBody) to a result or an error.
bool GroqSTT::handleReplyDone() {
  uint32_t total = millis() - _latencyStart;
  _latencyMs = (float)total;
  if (_sendMs > 0) _inferMs = (float)total - _sendMs;
  uint16_t s = _httpStatus;
  if (s == 200) {
    if (_rxBody.length() == 0) {
      finishError(STT_ERR_RESPONSE, "empty 200 reply");
      return false;
    }
    finishDone(_rxBody);
    return true;
  }
  String detail = "HTTP " + String(s);
  if (_rxBody.length()) {
    detail += ": ";
    detail += _rxBody.substring(0, 160);
    detail.trim();
  }
  if (s == 429) detail = "rate limited — retry later (HTTP 429, check Groq free-tier limits)";
  else if (s == 401 || s == 403) detail = "authentication failed (HTTP " + String(s) + ") — check your API key";
  finishError(STT_ERR_HTTP, detail);
  return false;
}

// Blocking wait for the reply, then normalise it into _result.
String GroqSTT::settleReply() {
  int r;
  while ((r = httpReadPoll()) == 0) delay(1);
  if (r < 0) {
    finishError(STT_ERR_RESPONSE, "reply timed out or truncated");
    return "";
  }
  handleReplyDone();
  return _result;
}

// ---------------------------------------------------------------------------
// DSP
// ---------------------------------------------------------------------------
int16_t GroqSTT::applyDsp(int16_t s) {
  float x = (float)s;
  if (_hpCoef != 0) {
    _hpY1 = _hpCoef * (_hpY1 + x - _hpX1);
    _hpX1 = x;
    x = _hpY1;
    _hpY2 = _hpCoef * (_hpY2 + x - _hpX2);
    _hpX2 = x;
  }
  float g = x * (float)_gain;
  if (g > 32767.0f) g = 32767.0f;
  else if (g < -32768.0f) g = -32768.0f;
  if (_softClip) {
    g = g / (1.0f + fabsf(g) / 32767.0f);
  }
  return (int16_t)g;
}

// ---------------------------------------------------------------------------
// blocking convenience
// ---------------------------------------------------------------------------
String GroqSTT::listen(uint32_t ms) {
  _fixedDuration = true;
  _fixedMs = ms;
  bool ok = startRecording();
  if (!ok) { _fixedDuration = false; return ""; }
  while (tick()) delay(1);
  _fixedDuration = false;
  return getResult();
}

String GroqSTT::listen() {
  _fixedDuration = false;
  if (_btnPin >= 0) {
    log(2, "hold BOOT (GPIO%d) to talk ...", _btnPin);
    // give TLS time to warm while waiting for the press
    uint32_t t0 = millis();
    while (digitalRead(_btnPin) == HIGH) {
      if (millis() - t0 > GROQ_STT_PREWARM_INTERVAL && isWifiConnected()) {
        // Probe only while we hold a real socket. After stop() the core leaves
        // fd 0 in place and any probe -> lazy setsockopt(0) -> EBADF spam.
        if (!httpSocketAlive()) httpEnsureConnected(); // prewarm: press then starts instantly
        t0 = millis();
      }
      delay(5);
    }
    delay(40); // debounce
    if (!startRecording()) return "";
    while (tick()) delay(1);
    return getResult();
  }
  // no button configured: with useVad(true) the recording stops ~1.2 s after
  // you stop talking; otherwise startRecording() applies a safety cap.
  if (!startRecording()) return "";
  while (tick()) delay(1);
  return getResult();
}

// ---------------------------------------------------------------------------
// user-supplied audio sources (blocking, bypass the mic entirely)
// ---------------------------------------------------------------------------
String GroqSTT::transcribeBuffer(const int16_t* pcm, size_t samples) {
  if (isBusy()) {
    finishError(STT_ERR_BUSY, "state machine active — wait for isDone()");
    return "";
  }
  _state = STT_STATE_IDLE;
  _lastError = STT_OK;
  _result = "";
  _errorDetail = "";
  _httpStatus = 0;
  if (!checkPrereqs()) return "";
  if (!httpEnsureConnected()) {
    finishError(STT_ERR_TLS_CONNECT, "could not reach " GROQ_STT_HOST);
    return "";
  }
  // real sample count known -> embed a WAV header with the true length
  if (!httpStartRequest((uint32_t)samples * 2)) {
    finishError(STT_ERR_TLS_CONNECT, "failed to send request head");
    return "";
  }
  uint8_t* buf = micChunkBuf();
  size_t i = 0;
  while (i < samples) {
    size_t n = samples - i;
    if (n > GROQ_STT_CHUNK_SAMPLES) n = GROQ_STT_CHUNK_SAMPLES;
    memcpy(buf, pcm + i, n * 2);
    if (!httpSendAudioChunk(buf, n * 2)) {
      finishError(STT_ERR_UPLOAD_ABORT, "socket died mid-upload");
      return "";
    }
    i += n;
  }
  if (!httpFinishUpload()) {
    finishError(STT_ERR_UPLOAD_ABORT, "failed to terminate upload");
    return "";
  }
  _sendMs = (float)(millis() - _latencyStart);
  _latencyStart = millis();
  return settleReply();
}

String GroqSTT::transcribeFile(fs::FS& fs, const char* path) {
  if (isBusy()) {
    finishError(STT_ERR_BUSY, "state machine active — wait for isDone()");
    return "";
  }
  _state = STT_STATE_IDLE;
  _lastError = STT_OK;
  _result = "";
  _errorDetail = "";
  _httpStatus = 0;
  if (!checkPrereqs()) return "";

  fs::File f = fs.open(path, "r");
  if (!f) {
    finishError(STT_ERR_FILE, String("cannot open '") + path + "'");
    return "";
  }
  // WAV files carry their own bytes (header + PCM) and pass through untouched;
  // anything else is treated as raw 16-bit mono PCM and gets a WAV header prepended.
  bool isWav = false;
  uint8_t magic[12];
  if (f.read(magic, 12) == 12) {
    isWav = memcmp(magic, "RIFF", 4) == 0 && memcmp(magic + 8, "WAVE", 4) == 0;
  }
  f.seek(0);

  if (!httpEnsureConnected()) {
    f.close();
    finishError(STT_ERR_TLS_CONNECT, "could not reach " GROQ_STT_HOST);
    return "";
  }
  if (!httpStartRequest(isWav ? 0 : 0xFFFFFFFF)) {  // pass-through file OR streamed raw PCM
    f.close();
    finishError(STT_ERR_TLS_CONNECT, "failed to send request head");
    return "";
  }
  uint8_t* buf = micChunkBuf();
  while (f.available()) {
    size_t n = f.read(buf, GROQ_STT_CHUNK_SAMPLES * 2);
    if (n == 0) break;
    if (!httpSendAudioChunk(buf, n)) {
      f.close();
      finishError(STT_ERR_UPLOAD_ABORT, "socket died mid-upload");
      return "";
    }
  }
  f.close();
  if (!httpFinishUpload()) {
    finishError(STT_ERR_UPLOAD_ABORT, "failed to terminate upload");
    return "";
  }
  _sendMs = (float)(millis() - _latencyStart);
  _latencyStart = millis();
  return settleReply();
}
