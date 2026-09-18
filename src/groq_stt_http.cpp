#include "groq_stt.h"
#include "groq_stt_config.h"

static const char s_boundary[] = "--groq_stt";   // multipart delimiter line (CRLF + boundary)

// ---------------------------------------------------------------------------
// byte-level helpers
// ---------------------------------------------------------------------------
static void w32(uint8_t* p, uint32_t v) {
  p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}
static void w16(uint8_t* p, uint16_t v) {
  p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
}

// 44-byte WAV header. dataLen == 0xFFFFFFFF => unknown length (streaming upload).
static void wavHeader(uint8_t* h, uint32_t rate, uint32_t dataLen) {
  memcpy(h, "RIFF", 4);
  w32(h + 4, dataLen == 0xFFFFFFFF ? 0xFFFFFFFF : dataLen + 36);
  memcpy(h + 8, "WAVE", 4);
  memcpy(h + 12, "fmt ", 4);
  w32(h + 16, 16);
  w16(h + 20, 1);   // PCM
  w16(h + 22, 1);   // mono
  w32(h + 24, rate);
  w32(h + 28, rate * 2);   // byte rate
  w16(h + 32, 2);          // block align
  w16(h + 34, 16);         // bits per sample
  memcpy(h + 36, "data", 4);
  w32(h + 40, dataLen == 0xFFFFFFFF ? 0xFFFFFFFF : dataLen);
}

static bool writeAll(WiFiClientSecure& c, const uint8_t* p, size_t len, uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (len) {
    if (millis() - t0 > timeoutMs) return false;
    size_t w = c.write(p, len);
    if (w == 0) {
      if (!c.connected()) return false;
      delay(1);
      continue;
    }
    p += w;
    len -= w;
  }
  return true;
}

// one HTTP/1.1 chunk: "<hexlen>\r\n<data>\r\n"
static bool writeChunk(WiFiClientSecure& c, const uint8_t* data, size_t len, uint32_t timeoutMs) {
  char head[16];
  int hn = snprintf(head, sizeof(head), "%zX\r\n", len);
  if (!writeAll(c, (const uint8_t*)head, (size_t)hn, timeoutMs)) return false;
  if (len && !writeAll(c, data, len, timeoutMs)) return false;
  return writeAll(c, (const uint8_t*)"\r\n", 2, timeoutMs);
}

// tri-state single byte read: 1 = got byte, 0 = still waiting (connected), -1 = failed
static int readByte(WiFiClientSecure& c, uint8_t& out, uint32_t deadline) {
  if (c.available() == 0) {
    if ((int32_t)(millis() - deadline) >= 0) return -1;
    if (!c.connected()) return -1;
    return 0;
  }
  int b = c.read();
  if (b < 0) return -1;
  out = (uint8_t)b;
  return 1;
}

// ---------------------------------------------------------------------------
// connection (pre-warm + request)
// ---------------------------------------------------------------------------
bool GroqSTT::httpEnsureConnected() {
  int fd = _client.fd();
  if (fd > 0 && _client.connected()) return true;
  if (fd > 0) _client.stop();        // drop the stale socket
  _httpOpen = false;
  if (_caCert) _client.setCACert(_caCert);
  else _client.setInsecure();
  uint32_t t0 = millis();
  // 3-arg connect bounds TCP + TLS handshake; setTimeout() BEFORE connect would
  // make core 3.x setsockopt on a not-yet-created socket (EBADF log spam).
  if (!_client.connect(GROQ_STT_HOST, GROQ_STT_PORT, GROQ_STT_CONNECT_TIMEOUT))
    return false;
  _connectMs = (float)(millis() - t0);
  log(3, "TLS handshake in %lu ms", (unsigned long)_connectMs);
  uint8_t dummy;
  _client.read(&dummy, 0);    // prime SO timeouts while the socket is fresh
  _httpOpen = _client.fd() > 0;
  return _httpOpen;
}

// True only while a real socket is open. Core 3.x wipes the fd to 0 inside
// stop() (memset in stop_ssl_socket), so a post-stop probe makes its lazy
// setsockopt(0) log "setSocketOption(): fail on 0, errno: 9". Callers must
// never probe connected()/read()/write() on fd 0.
bool GroqSTT::httpSocketAlive() {
  int fd = _client.fd();
  if (fd <= 0) return false;
  return _client.connected();
}

// Server sends "Connection: close" and drops the link after every reply. Close
// our side immediately when the reply is fully read: probing a stale half-dead
// socket makes core 3.x log "setSocketOption(): fail on 0, errno: 9" (it lazily
// applies SO timeouts on the already-freed fd). Real failures surface through
// our own error path, so there is nothing to keep the socket alive for.
void GroqSTT::httpClose() {
  _client.stop();
  _httpOpen = false;
}

void GroqSTT::httpAbort() {
  _client.stop();
  _httpOpen = false;
}

// ---------------------------------------------------------------------------
// request head + multipart fields + WAV header (all in the first chunk)
// ---------------------------------------------------------------------------
bool GroqSTT::httpStartRequest(uint32_t wavDataLen) {
  String head;
  head.reserve(384);
  head += "POST ";
  head += _translate ? GROQ_STT_PATH_TRANSLATE : GROQ_STT_PATH;
  head += " HTTP/1.1\r\n";
  head += "Host: " GROQ_STT_HOST "\r\n";
  head += "Authorization: Bearer ";
  head += _apiKey;
  head += "\r\n";
  head += "User-Agent: " GROQ_STT_AGENT "\r\n";   // Groq rejects requests with no User-Agent
  head += "Content-Type: multipart/form-data; boundary=";
  head += (s_boundary + 2);                        // token WITHOUT the leading "--"
  head += "\r\n";
  head += "Transfer-Encoding: chunked\r\n";
  head += "Connection: close\r\n";
  head += "\r\n";
  if (!writeAll(_client, (const uint8_t*)head.c_str(), head.length(), GROQ_STT_CONNECT_TIMEOUT))
    return false;

  String body;
  body.reserve(768);
  const char* fmtName =
      _format == STT_FMT_JSON ? "json" : (_format == STT_FMT_VERBOSE_JSON ? "verbose_json" : "text");

  // model (translate forces the one model Groq allows on /translations)
  body += s_boundary; body += "\r\n";
  body += "Content-Disposition: form-data; name=\"model\"\r\n\r\n";
  body += _translate ? GROQ_STT_TRANSLATE_MODEL : _model;
  body += "\r\n";
  // language (optional -> auto-detect)
  if (_language.length()) {
    body += s_boundary; body += "\r\n";
    body += "Content-Disposition: form-data; name=\"language\"\r\n\r\n";
    body += _language; body += "\r\n";
  }
  // response_format
  body += s_boundary; body += "\r\n";
  body += "Content-Disposition: form-data; name=\"response_format\"\r\n\r\n";
  body += fmtName; body += "\r\n";
  // prompt
  if (_prompt.length()) {
    body += s_boundary; body += "\r\n";
    body += "Content-Disposition: form-data; name=\"prompt\"\r\n\r\n";
    body += _prompt; body += "\r\n";
  }
  // temperature (0 = greedy; only sent when the user picked something else)
  if (_temperature > 0.0f) {
    body += s_boundary; body += "\r\n";
    body += "Content-Disposition: form-data; name=\"temperature\"\r\n\r\n";
    body += String(_temperature, 3); body += "\r\n";
  }
  // file part: header (audio follows in later chunks)
  body += s_boundary; body += "\r\n";
  body += "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n";
  body += "Content-Type: audio/wav\r\n\r\n";
  if (!writeChunk(_client, (const uint8_t*)body.c_str(), body.length(), GROQ_STT_CONNECT_TIMEOUT))
    return false;

  if (wavDataLen != 0) {                 // 0 = the file stream already carries its own bytes
    uint8_t wav[44];
    wavHeader(wav, GROQ_STT_SAMPLE_RATE, wavDataLen);
    if (!writeChunk(_client, wav, 44, GROQ_STT_CONNECT_TIMEOUT))
      return false;
  }

  return true;
}

bool GroqSTT::httpSendAudioChunk(const uint8_t* data, size_t len) {
  return writeChunk(_client, data, len, GROQ_STT_READ_TIMEOUT);
}

// final multipart boundary + terminating 0-chunk
bool GroqSTT::httpFinishUpload() {
  String tail = "\r\n";
  tail += s_boundary;
  tail += "--\r\n";
  if (!writeChunk(_client, (const uint8_t*)tail.c_str(), tail.length(), GROQ_STT_READ_TIMEOUT))
    return false;
  if (!writeAll(_client, (const uint8_t*)"0\r\n\r\n", 5, GROQ_STT_READ_TIMEOUT))
    return false;
  _rxState = GROQ_STT_RX_STATUS;   // fresh reply parse for THIS request (httpReadPoll re-inits on entry)
  return true;
}

// ---------------------------------------------------------------------------
// reply reader (incremental pump; safe to call from a loop)
//   returns 0 while reading, 1 when complete, -1 on error
//   results land in _rxStatus / _rxBody / _httpStatus
// ---------------------------------------------------------------------------
int GroqSTT::httpReadPoll() {
  if (_rxState == GROQ_STT_RX_STATUS) {          // fresh parse for this request
    _rxLine = "";
    _rxBody = "";
    _rxStatus = 0;
    _rxChunked = false;
    _rxRemain = 0;
    _rxDeadline = millis() + GROQ_STT_READ_TIMEOUT;
  }

  uint8_t b;

  while (true) {
    switch (_rxState) {
      // ---- status line ----
      case GROQ_STT_RX_STATUS: {
        int r;
        while ((r = readByte(_client, b, _rxDeadline)) == 1) {
          if (b == '\n') {
            int code = 0;
            if (sscanf(_rxLine.c_str(), "HTTP/%*s %d", &code) != 1) {
              httpAbort();
              return -1;
            }
            _rxStatus = (uint16_t)code;
            _rxLine = "";
            _rxState = GROQ_STT_RX_HEADERS;
            goto next;
          }
          if (b != '\r' && _rxLine.length() < 128) _rxLine += (char)b;
        }
        if (r < 0) { httpAbort(); return -1; }
        return 0;
      }

      // ---- headers ----
      case GROQ_STT_RX_HEADERS: {
        int r;
        while ((r = readByte(_client, b, _rxDeadline)) == 1) {
          if (b == '\n') {
            if (_rxLine.length() == 0) {
              // end of headers: decide how the body arrives
              _rxBody.reserve(512);
              if (_rxChunked) { _rxState = GROQ_STT_RX_CHUNK_SIZE; }
              else if (_rxRemain > 0) { _rxState = GROQ_STT_RX_RAW; }
              else if (_rxStatus == 200) { _httpStatus = _rxStatus; httpClose(); return 1; } // no body
              else { _rxState = GROQ_STT_RX_RAW; } // close-delimited body
              _rxLine = "";
              goto next;
            }
            String low = _rxLine;
            low.toLowerCase();
            if (low.startsWith("transfer-encoding") && low.indexOf("chunked") >= 0)
              _rxChunked = true;
            else if (low.startsWith("content-length"))
              _rxRemain = (size_t)atol(_rxLine.c_str() + 15);
            _rxLine = "";
          } else if (b != '\r') {
            if (_rxLine.length() < 256) _rxLine += (char)b;
          }
        }
        if (r < 0) { httpAbort(); return -1; }
        return 0;
      }

      // ---- chunk size line ----
      case GROQ_STT_RX_CHUNK_SIZE: {
        int r;
        while ((r = readByte(_client, b, _rxDeadline)) == 1) {
          if (b == '\n') {
            String hex = _rxLine;
            int semi = hex.indexOf(';');
            if (semi >= 0) hex = hex.substring(0, semi);
            char* endp = nullptr;
            long sz = strtol(hex.c_str(), &endp, 16);
            if (endp == hex.c_str()) { httpAbort(); return -1; } // not hex
            _rxLine = "";
            if (sz <= 0) { _rxState = GROQ_STT_RX_TRAILER; }
            else { _rxRemain = (size_t)sz; _rxState = GROQ_STT_RX_CHUNK_DATA; }
            goto next;
          }
          if (b != '\r' && _rxLine.length() < 32) _rxLine += (char)b;
        }
        if (r < 0) { httpAbort(); return -1; }
        return 0;
      }

      // ---- chunk payload ----
      case GROQ_STT_RX_CHUNK_DATA: {
        int avail = _client.available();
        if (avail == 0) {
          if (!_client.connected()) { httpAbort(); return -1; }
          if ((int32_t)(millis() - _rxDeadline) >= 0) { httpAbort(); return -1; }
          return 0;
        }
        size_t want = _rxRemain < 512 ? _rxRemain : 512;
        if ((size_t)avail < want) want = (size_t)avail;
        uint8_t tmp[512];
        int n = _client.read(tmp, want);
        if (n <= 0) { httpAbort(); return -1; }
        _rxBody.concat((const char*)tmp, (size_t)n);
        _rxRemain -= (size_t)n;
        if (_rxRemain == 0) _rxState = GROQ_STT_RX_CHUNK_CRLF;
        continue;
      }

      // ---- CRLF after chunk ----
      case GROQ_STT_RX_CHUNK_CRLF: {
        int r = readByte(_client, b, _rxDeadline);
        if (r == 1) {
          if (b == '\r') { _rxState = GROQ_STT_RX_CHUNK_LF; goto next; }
          if (b == '\n') { _rxState = GROQ_STT_RX_CHUNK_SIZE; goto next; }
        }
        if (r == 0) return 0;
        httpAbort();
        return -1;
      }

      // ---- LF after CR in chunk CRLF ----
      case GROQ_STT_RX_CHUNK_LF: {
        int r = readByte(_client, b, _rxDeadline);
        if (r == 1) {
          if (b == '\n') { _rxState = GROQ_STT_RX_CHUNK_SIZE; goto next; }
        }
        if (r == 0) return 0;
        httpAbort();
        return -1;
      }

      // ---- trailers (after 0-size chunk); blank line or close ends it ----
      case GROQ_STT_RX_TRAILER: {
        int r = readByte(_client, b, _rxDeadline);
        if (r == 1) {
          if (b == '\n') {
            if (_rxLine.length() == 0) { _httpStatus = _rxStatus; httpClose(); return 1; } // done
            _rxLine = "";
          } else if (b != '\r') {
            if (_rxLine.length() < 256) _rxLine += (char)b;
          }
          continue;
        }
        if (r < 0 && !_client.connected()) {
          _httpStatus = _rxStatus;  // server closed: body already fully read
          httpClose();
          return 1;
        }
        if (r == 0) return 0;
        httpAbort();
        return -1;
      }

      // ---- content-length or close-delimited body ----
      case GROQ_STT_RX_RAW: {
        int avail = _client.available();
        if (avail == 0) {
          if ((int32_t)(millis() - _rxDeadline) >= 0) { httpAbort(); return -1; }
          if (_rxRemain == 0) {
            if (!_client.connected()) { _httpStatus = _rxStatus; httpClose(); return 1; }
            return 0;
          }
          if (!_client.connected()) { httpAbort(); return -1; } // truncated
          return 0;
        }
        size_t want = 512;
        if (_rxRemain && _rxRemain < want) want = _rxRemain;
        if ((size_t)avail < want) want = (size_t)avail;
        uint8_t tmp[512];
        int n = _client.read(tmp, want);
        if (n <= 0) {
          if (!_client.connected()) {
            if (_rxRemain == 0) { _httpStatus = _rxStatus; httpClose(); return 1; }
            httpAbort();
            return -1;
          }
          return 0;
        }
        _rxBody.concat((const char*)tmp, (size_t)n);
        if (_rxRemain > 0) {
          _rxRemain -= (size_t)n;
          if (_rxRemain == 0) { _httpStatus = _rxStatus; httpClose(); return 1; }
        }
        continue;
      }

      default:
        httpAbort();
        return -1;
    }
next:
    ; // restart switch on the newly set state
  }
}
