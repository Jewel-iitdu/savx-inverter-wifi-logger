// probe.cpp - run the firmware's own meter code (src/meter/TuyaProto.cpp, MeterLive.cpp) against the real meter
// from the Mac, before flashing. Reads .meter/secret.json (tools/meter/get-key.sh); never prints the key.
// Build and run: tools/meter/probe.sh [ip] [33|34|35]
#include <arpa/inet.h>
#include <math.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <ArduinoJson.h>
#include <fstream>
#include <sstream>
#include <string>

#include "meter/MeterLive.h"
#include "meter/TuyaProto.h"
#include "model/JsonOut.h"

struct SslCrypto : tuya::Crypto {
  bool ecb(bool enc, const uint8_t key[16], const uint8_t* in, size_t n, uint8_t* out) override {
    EVP_CIPHER_CTX* x = EVP_CIPHER_CTX_new();
    int l = 0, l2 = 0;
    bool ok = EVP_CipherInit_ex(x, EVP_aes_128_ecb(), nullptr, key, nullptr, enc ? 1 : 0) &&
              EVP_CIPHER_CTX_set_padding(x, 0) && EVP_CipherUpdate(x, out, &l, in, (int)n) &&
              EVP_CipherFinal_ex(x, out + l, &l2);
    EVP_CIPHER_CTX_free(x);
    return ok;
  }
  bool hmac(const uint8_t key[16], const uint8_t* d, size_t n, uint8_t out[32]) override {
    unsigned int l = 0;
    return HMAC(EVP_sha256(), key, 16, d, n, out, &l) && l == 32;
  }
  bool gcm(bool enc, const uint8_t key[16], const uint8_t iv[12], const uint8_t* aad, size_t aadN, const uint8_t* in,
           size_t n, uint8_t* out, uint8_t* tag) {
    EVP_CIPHER_CTX* x = EVP_CIPHER_CTX_new();
    int l = 0;
    bool ok = EVP_CipherInit_ex(x, EVP_aes_128_gcm(), nullptr, nullptr, nullptr, enc ? 1 : 0) &&
              EVP_CIPHER_CTX_ctrl(x, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) &&
              EVP_CipherInit_ex(x, nullptr, nullptr, key, iv, enc ? 1 : 0);
    if (ok && aadN) ok = EVP_CipherUpdate(x, nullptr, &l, aad, (int)aadN);
    if (ok && n) ok = EVP_CipherUpdate(x, out, &l, in, (int)n);
    if (ok && !enc) ok = EVP_CIPHER_CTX_ctrl(x, EVP_CTRL_GCM_SET_TAG, 16, tag);
    if (ok) ok = EVP_CipherFinal_ex(x, out + n, &l) > 0;
    if (ok && enc) ok = EVP_CIPHER_CTX_ctrl(x, EVP_CTRL_GCM_GET_TAG, 16, tag);
    EVP_CIPHER_CTX_free(x);
    return ok;
  }
  bool gcmEnc(const uint8_t key[16], const uint8_t iv[12], const uint8_t* aad, size_t aadN, const uint8_t* in,
              size_t n, uint8_t* out, uint8_t tag[16]) override {
    return gcm(true, key, iv, aad, aadN, in, n, out, tag);
  }
  bool gcmDec(const uint8_t key[16], const uint8_t iv[12], const uint8_t* aad, size_t aadN, const uint8_t* in,
              size_t n, const uint8_t tag[16], uint8_t* out) override {
    uint8_t t[16];
    memcpy(t, tag, 16);
    return gcm(false, key, iv, aad, aadN, in, n, out, t);
  }
  void random(uint8_t* out, size_t n) override { arc4random_buf(out, n); }
};

static uint8_t rx[tuya::MAX_FRAME + 100], tx[512];

// frames until on(f) is true; prints every frame's command and payload (the payload is the meter's data, no key)
template <typename F>
static bool readUntil(int fd, tuya::Session& s, size_t& have, int ms, F on) {
  for (;;) {
    while (have) {
      tuya::Frame f;
      size_t used = 0;
      tuya::Parse p = s.parse(rx, have, f, used);
      if (p == tuya::Parse::MORE) break;
      if (p == tuya::Parse::SKIP) used = 1;
      if (p == tuya::Parse::BAD) printf("  frame failed its check (wrong key or protocol)\n");
      bool done = false;
      if (p == tuya::Parse::OK) {
        printf("  frame cmd %u retcode %u, %zu bytes: %.*s\n", f.cmd, f.retcode, f.n,
               f.cmd == tuya::CMD_SESS_RESP ? 0 : (int)f.n, (const char*)f.payload);
        done = on(f);
      }
      memmove(rx, rx + used, have - used);
      have -= used;
      if (done) return true;
    }
    pollfd pf = {fd, POLLIN, 0};
    if (poll(&pf, 1, ms) <= 0) return false;
    ssize_t r = recv(fd, rx + have, sizeof rx - have, 0);
    if (r <= 0) return false;
    have += (size_t)r;
  }
}

int main(int argc, char** argv) {
  std::ifstream in(".meter/secret.json");
  if (!in) {
    fprintf(stderr, "run from the repo root after tools/meter/get-key.sh\n");
    return 2;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  JsonDocument sec;
  if (deserializeJson(sec, ss.str())) return 2;
  std::string id = sec["id"] | "", key = sec["key"] | "", ip = sec["ip"] | "";
  int proto = 0;
  if (argc > 1) ip = argv[1];
  if (argc > 2) proto = atoi(argv[2]);
  else if (sec["ver"].is<float>()) proto = (int)(sec["ver"].as<float>() * 10 + 0.5);
  if (key.size() != 16 || (proto != 33 && proto != 34 && proto != 35)) {
    fprintf(stderr, "need a 16-character key and protocol 33/34/35\n");
    return 2;
  }
  SslCrypto c;
  printf("crypto self-test: %s\n", tuya::selfTest(c) ? "ok" : "FAILED");
  printf("meter %s protocol %d.%d\n", ip.c_str(), proto / 10, proto % 10);
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(6668);
  inet_pton(AF_INET, ip.c_str(), &a.sin_addr);
  if (connect(fd, (sockaddr*)&a, sizeof a)) {
    perror("connect");
    return 1;
  }
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  tuya::Session s(c);
  s.begin(proto, (const uint8_t*)key.data(), id.c_str());
  size_t have = 0, n;
  if (s.needsHandshake()) {
    n = s.buildStart(tx, sizeof tx);
    send(fd, tx, n, 0);
    size_t fin = 0;
    if (!readUntil(fd, s, have, 4000, [&](const tuya::Frame& f) {
          if (f.cmd != tuya::CMD_SESS_RESP) return false;
          fin = s.onSessResp(f, tx, sizeof tx);
          return true;
        }) || !fin) {
      printf("handshake FAILED\n");
      return 1;
    }
    printf("handshake ok\n");
    send(fd, tx, fin, 0);
  }
  n = s.buildQuery(tx, sizeof tx, (uint32_t)time(nullptr));
  send(fd, tx, n, 0);
  MeterLive m;
  bool got = readUntil(fd, s, have, 4000, [&](const tuya::Frame& f) {
    return f.n && meterDecodeDps((const char*)f.payload, f.n, m);
  });
  if (!got) {
    printf("no status reply\n");
    return 1;
  }
  n = s.buildRefresh(tx, sizeof tx, 6);
  send(fd, tx, n, 0);
  readUntil(fd, s, have, 4000, [&](const tuya::Frame& f) {
    MeterLive t = m;
    if (!f.n || !meterDecodeDps((const char*)f.payload, f.n, t)) return false;
    m = t;
    return !isnan(m.w);
  });
  close(fd);
  char b[400];
  JsonOut j(b, sizeof b);
  j.open();
  meterLiveFields(j, m);
  j.close();
  printf("decoded for the cloud: %s\n", b);
  return 0;
}
