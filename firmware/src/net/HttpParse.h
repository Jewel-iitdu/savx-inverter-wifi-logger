// HttpParse.h - incremental HTTP/1.1 response parser for the uplink's own HTTPS client. Host-portable.
//
// Why not HTTPClient: on arduino-esp32 3.3.12 getString()/writeToStream() can hang when a server stops mid-body
// (fixed upstream after 3.3.12, r8c M1) and a followed 302 re-sends a stale Content-Length (#8300, r8c C1). This
// parser reads exactly Content-Length bytes or the chunked framing, so the caller always knows when a response is
// complete and can keep the TLS connection alive (d1 §4.6: Google advises keep-alive over REST churn).
#pragma once
#include <stddef.h>
#include <stdint.h>

class HttpParse {
 public:
  enum class R : uint8_t { MORE, DONE, ERROR };

  // body: buffer for the response body (NUL-terminated, truncated to cap-1 bytes; the rest is drained).
  // headersOnly: DONE as soon as the header block ends (the caller closes the connection, e.g. the mailer nudge).
  void reset(char* body, size_t cap, bool headersOnly = false, bool headRequest = false);
  R feed(const uint8_t* p, size_t n);
  R finishOnClose();  // the peer closed the connection: DONE for "read until close" bodies, else ERROR

  int status() const { return status_; }
  bool keepAlive() const { return keepAlive_; }
  bool chunked() const { return chunked_; }
  long contentLength() const { return contentLength_; }
  size_t bodyLen() const { return bodyLen_; }       // bytes stored
  size_t bodyTotal() const { return bodyTotal_; }   // bytes received (stored + drained)
  bool truncated() const { return bodyTotal_ > bodyLen_; }
  bool headersDone() const { return st_ > S_HEADERS; }
  const char* location() const { return location_; }
  const char* error() const { return err_; }

 private:
  enum S : uint8_t { S_STATUS, S_HEADERS, S_BODY_LEN, S_CHUNK_SIZE, S_CHUNK_DATA, S_CHUNK_CRLF, S_TRAILER, S_BODY_CLOSE,
                     S_DONE, S_ERROR };
  R fail(const char* e);
  bool line(char c);  // accumulate a header/status line; true when complete
  void header(char* l);
  void store(uint8_t c);
  R endHeaders();

  char* body_ = nullptr;
  size_t cap_ = 0, bodyLen_ = 0, bodyTotal_ = 0;
  bool headersOnly_ = false, head_ = false;
  S st_ = S_STATUS;
  char ln_[192];
  size_t lnLen_ = 0;
  bool lnOverflow_ = false;
  int status_ = 0;
  bool keepAlive_ = true, chunked_ = false, http10_ = false;
  long contentLength_ = -1;
  unsigned long chunkLeft_ = 0;
  long bodyLeft_ = 0;
  char location_[160] = "";
  const char* err_ = "";
};
