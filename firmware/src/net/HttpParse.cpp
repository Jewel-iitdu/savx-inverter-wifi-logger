// HttpParse.cpp - see HttpParse.h.
#include "net/HttpParse.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool ieq(const char* a, const char* b) {
  for (; *a && *b; a++, b++)
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
  return *a == *b;
}

static bool icontains(const char* hay, const char* needle) {
  size_t n = strlen(needle);
  for (; *hay; hay++) {
    size_t i = 0;
    while (i < n && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i])) i++;
    if (i == n) return true;
  }
  return false;
}

void HttpParse::reset(char* body, size_t cap, bool headersOnly, bool headRequest) {
  body_ = body;
  cap_ = cap;
  bodyLen_ = bodyTotal_ = 0;
  if (body_ && cap_) body_[0] = 0;
  headersOnly_ = headersOnly;
  head_ = headRequest;
  st_ = S_STATUS;
  lnLen_ = 0;
  lnOverflow_ = false;
  status_ = 0;
  keepAlive_ = true;
  chunked_ = false;
  http10_ = false;
  contentLength_ = -1;
  chunkLeft_ = 0;
  bodyLeft_ = 0;
  location_[0] = 0;
  err_ = "";
}

HttpParse::R HttpParse::fail(const char* e) {
  st_ = S_ERROR;
  err_ = e;
  keepAlive_ = false;
  return R::ERROR;
}

bool HttpParse::line(char c) {
  if (c == '\n') {
    if (lnLen_ && ln_[lnLen_ - 1] == '\r') lnLen_--;
    ln_[lnLen_] = 0;
    return true;
  }
  if (lnLen_ < sizeof ln_ - 1) ln_[lnLen_++] = c;
  else lnOverflow_ = true;  // over-long header line: keep the start, ignore the rest
  return false;
}

void HttpParse::store(uint8_t c) {
  bodyTotal_++;
  if (body_ && bodyLen_ + 1 < cap_) {
    body_[bodyLen_++] = (char)c;
    body_[bodyLen_] = 0;
  }
}

void HttpParse::header(char* l) {
  char* colon = strchr(l, ':');
  if (!colon) return;
  *colon = 0;
  char* v = colon + 1;
  while (*v == ' ' || *v == '\t') v++;
  size_t vl = strlen(v);
  while (vl && (v[vl - 1] == ' ' || v[vl - 1] == '\t')) v[--vl] = 0;
  if (ieq(l, "Content-Length")) contentLength_ = strtol(v, nullptr, 10);
  else if (ieq(l, "Transfer-Encoding")) chunked_ = icontains(v, "chunked");
  else if (ieq(l, "Connection")) {
    if (icontains(v, "close")) keepAlive_ = false;
    else if (icontains(v, "keep-alive")) keepAlive_ = true;
  } else if (ieq(l, "Location")) snprintf(location_, sizeof location_, "%s", v);
}

HttpParse::R HttpParse::endHeaders() {
  if (headersOnly_) {
    st_ = S_DONE;
    keepAlive_ = false;  // the caller drops the connection without reading the body
    return R::DONE;
  }
  bool noBody = head_ || status_ == 204 || status_ == 304 || (status_ >= 100 && status_ < 200);
  if (noBody) {
    st_ = S_DONE;
    return R::DONE;
  }
  if (chunked_) {
    st_ = S_CHUNK_SIZE;
    return R::MORE;
  }
  if (contentLength_ >= 0) {
    bodyLeft_ = contentLength_;
    if (bodyLeft_ == 0) {
      st_ = S_DONE;
      return R::DONE;
    }
    st_ = S_BODY_LEN;
    return R::MORE;
  }
  keepAlive_ = false;  // neither length nor chunked: the body ends when the server closes
  st_ = S_BODY_CLOSE;
  return R::MORE;
}

HttpParse::R HttpParse::feed(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    uint8_t c = p[i];
    switch (st_) {
      case S_STATUS:
        if (!line((char)c)) break;
        if (strncmp(ln_, "HTTP/1.", 7) != 0 || strlen(ln_) < 12) return fail("bad status line");
        http10_ = ln_[7] == '0';
        if (http10_) keepAlive_ = false;
        status_ = atoi(ln_ + 9);
        if (status_ < 100 || status_ > 599) return fail("bad status code");
        lnLen_ = 0;
        lnOverflow_ = false;
        st_ = S_HEADERS;
        break;
      case S_HEADERS:
        if (!line((char)c)) break;
        if (lnLen_ == 0) {
          if (status_ >= 100 && status_ < 200 && status_ != 101) {  // interim response: a real one follows
            st_ = S_STATUS;
            break;
          }
          R r = endHeaders();
          if (r != R::MORE) return r;
        } else {
          header(ln_);
        }
        lnLen_ = 0;
        lnOverflow_ = false;
        break;
      case S_BODY_LEN:
        store(c);
        if (--bodyLeft_ <= 0) {
          st_ = S_DONE;
          return R::DONE;
        }
        break;
      case S_CHUNK_SIZE:
        if (!line((char)c)) break;
        {
          char* end = nullptr;
          if (!isxdigit((unsigned char)ln_[0])) return fail("bad chunk size");
          chunkLeft_ = strtoul(ln_, &end, 16);
          lnLen_ = 0;
          st_ = chunkLeft_ ? S_CHUNK_DATA : S_TRAILER;
        }
        break;
      case S_CHUNK_DATA:
        store(c);
        if (--chunkLeft_ == 0) st_ = S_CHUNK_CRLF;
        break;
      case S_CHUNK_CRLF:
        if (c == '\n') st_ = S_CHUNK_SIZE;
        else if (c != '\r') return fail("bad chunk end");
        break;
      case S_TRAILER:
        if (!line((char)c)) break;
        if (lnLen_ == 0) {
          st_ = S_DONE;
          return R::DONE;
        }
        lnLen_ = 0;
        break;
      case S_BODY_CLOSE:
        store(c);
        break;
      case S_DONE:
        return R::DONE;  // extra bytes after a complete response: ignored (no pipelining is used)
      case S_ERROR:
        return R::ERROR;
    }
  }
  if (st_ == S_DONE) return R::DONE;
  if (st_ == S_ERROR) return R::ERROR;
  return R::MORE;
}

HttpParse::R HttpParse::finishOnClose() {
  keepAlive_ = false;
  if (st_ == S_DONE) return R::DONE;
  if (st_ == S_BODY_CLOSE) {
    st_ = S_DONE;
    return R::DONE;
  }
  return fail(st_ <= S_HEADERS ? "closed before headers" : "closed mid-body");
}
