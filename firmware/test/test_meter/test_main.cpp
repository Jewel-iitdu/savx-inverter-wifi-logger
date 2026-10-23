// test_meter - grid meter (0.9.0): Tuya LAN protocol 3.3 / 3.4 / 3.5 against frames made by tinytuya 1.20
// (vectors.h, tools/meter/gen_vectors.py), data-point decoding and the day accounting of the kWh counter.
// Crypto: OpenSSL here, mbedtls on the device (src/meter/MeterCrypto.cpp, with the same known-answer check).
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <string>

#include "meter/MeterDay.h"
#include "meter/MeterLive.h"
#include "meter/TuyaProto.h"
#include "model/JsonOut.h"
#include "model/TimeUtil.h"
#include "vectors.h"

void setUp() {}
void tearDown() {}

struct SslCrypto : tuya::Crypto {
  uint8_t ctr = 0x40;  // deterministic "random" (the vectors use the same sequence)
  bool ecb(bool enc, const uint8_t key[16], const uint8_t* in, size_t n, uint8_t* out) override {
    EVP_CIPHER_CTX* x = EVP_CIPHER_CTX_new();
    int l = 0, l2 = 0;
    bool ok = EVP_CipherInit_ex(x, EVP_aes_128_ecb(), nullptr, key, nullptr, enc ? 1 : 0) &&
              EVP_CIPHER_CTX_set_padding(x, 0) && EVP_CipherUpdate(x, out, &l, in, (int)n) &&
              EVP_CipherFinal_ex(x, out + l, &l2);
    EVP_CIPHER_CTX_free(x);
    return ok && (size_t)(l + l2) == n;
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
  void random(uint8_t* out, size_t n) override {
    for (size_t i = 0; i < n; i++) out[i] = ctr++;
  }
};

static const uint8_t* key() { return (const uint8_t*)V_KEY; }

static void assertBytes(const uint8_t* want, size_t wantN, const uint8_t* got, size_t gotN) {
  TEST_ASSERT_EQUAL_UINT32(wantN, gotN);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, wantN);
}

// Parses a whole device frame (copied, it is decrypted in place) and returns the payload as a string.
static std::string parseOk(tuya::Session& s, const uint8_t* fr, size_t n, uint32_t wantCmd) {
  static uint8_t buf[2048];
  memcpy(buf, fr, n);
  tuya::Frame f;
  size_t used = 0;
  // a short read first: the parser must ask for the whole frame
  TEST_ASSERT_EQUAL_INT((int)tuya::Parse::MORE, (int)s.parse(buf, 10, f, used));
  TEST_ASSERT_TRUE(used >= 16);
  TEST_ASSERT_EQUAL_INT((int)tuya::Parse::MORE, (int)s.parse(buf, n - 1, f, used));
  TEST_ASSERT_EQUAL_UINT32(n, used);
  TEST_ASSERT_EQUAL_INT((int)tuya::Parse::OK, (int)s.parse(buf, n, f, used));
  TEST_ASSERT_EQUAL_UINT32(n, used);
  TEST_ASSERT_EQUAL_UINT32(wantCmd, f.cmd);
  TEST_ASSERT_EQUAL_UINT32(0, f.retcode);
  return std::string((const char*)f.payload, f.n);
}

void test_crc32_check_value() {
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, tuya::crc32((const uint8_t*)"123456789", 9));
}

void test_crypto_self_test() {
  SslCrypto c;
  TEST_ASSERT_TRUE(tuya::selfTest(c));
}

void test_v33_query_and_responses() {
  SslCrypto c;
  tuya::Session s(c);
  s.begin(33, key(), V_DEV);
  TEST_ASSERT_FALSE(s.needsHandshake());
  TEST_ASSERT_TRUE(s.ready());
  uint8_t out[512];
  size_t n = s.buildQuery(out, sizeof out, V_T);
  assertBytes(V33_QUERY, sizeof V33_QUERY, out, n);
  n = s.buildRefresh(out, sizeof out, 6);
  assertBytes(V33_REFRESH, sizeof V33_REFRESH, out, n);
  std::string p = parseOk(s, V33_RESP, sizeof V33_RESP, tuya::CMD_DP_QUERY);
  TEST_ASSERT_TRUE(p.find("\"dps\":{\"1\":30503") != std::string::npos);
  p = parseOk(s, V33_PUSH, sizeof V33_PUSH, tuya::CMD_STATUS);  // "3.3" header in front of the ciphertext
  TEST_ASSERT_EQUAL_INT('{', p[0]);
  TEST_ASSERT_TRUE(p.find("\"16\":true") != std::string::npos);
}

static void handshake(int ver, const uint8_t* start, size_t startN, const uint8_t* resp, size_t respN,
                      const uint8_t* fin, size_t finN, const uint8_t* query, size_t queryN, const uint8_t* refresh,
                      size_t refreshN, const uint8_t* dresp, size_t drespN) {
  SslCrypto c;
  tuya::Session s(c);
  s.begin(ver, key(), V_DEV);
  TEST_ASSERT_TRUE(s.needsHandshake());
  TEST_ASSERT_FALSE(s.ready());
  uint8_t out[512], buf[512];
  size_t n = s.buildStart(out, sizeof out);
  assertBytes(start, startN, out, n);
  memcpy(buf, resp, respN);
  tuya::Frame f;
  size_t used;
  TEST_ASSERT_EQUAL_INT((int)tuya::Parse::OK, (int)s.parse(buf, respN, f, used));
  n = s.onSessResp(f, out, sizeof out);
  assertBytes(fin, finN, out, n);
  TEST_ASSERT_TRUE(s.ready());
  n = s.buildQuery(out, sizeof out, V_T);
  assertBytes(query, queryN, out, n);
  n = s.buildRefresh(out, sizeof out, 6);
  assertBytes(refresh, refreshN, out, n);
  std::string p = parseOk(s, dresp, drespN, tuya::CMD_DP_QUERY_NEW);  // version header inside the ciphertext
  TEST_ASSERT_EQUAL_INT('{', p[0]);
  TEST_ASSERT_TRUE(p.find("\"data\":{\"dps\":{\"1\":30503") != std::string::npos);
}

void test_v34_handshake_and_query() {
  handshake(34, V34_START, sizeof V34_START, V34_SESSRESP, sizeof V34_SESSRESP, V34_FINISH, sizeof V34_FINISH,
            V34_QUERY, sizeof V34_QUERY, V34_REFRESH, sizeof V34_REFRESH, V34_RESP, sizeof V34_RESP);
}

void test_v35_handshake_and_query() {
  handshake(35, V35_START, sizeof V35_START, V35_SESSRESP, sizeof V35_SESSRESP, V35_FINISH, sizeof V35_FINISH,
            V35_QUERY, sizeof V35_QUERY, V35_REFRESH, sizeof V35_REFRESH, V35_RESP, sizeof V35_RESP);
}

void test_wrong_key_is_rejected() {
  SslCrypto c;
  tuya::Session s(c);
  uint8_t wrong[16];
  memcpy(wrong, key(), 16);
  wrong[0] ^= 1;
  uint8_t buf[512];
  tuya::Frame f;
  size_t used;
  // 3.3: CRC fine, decryption garbage -> BAD (bad padding) or a payload that is not JSON
  s.begin(33, wrong, V_DEV);
  memcpy(buf, V33_RESP, sizeof V33_RESP);
  tuya::Parse r = s.parse(buf, sizeof V33_RESP, f, used);
  TEST_ASSERT_TRUE(r == tuya::Parse::BAD || (f.n && f.payload[0] != '{'));
  // 3.4: HMAC mismatch
  s.begin(34, wrong, V_DEV);
  uint8_t out[256];
  s.buildStart(out, sizeof out);
  memcpy(buf, V34_SESSRESP, sizeof V34_SESSRESP);
  TEST_ASSERT_EQUAL_INT((int)tuya::Parse::BAD, (int)s.parse(buf, sizeof V34_SESSRESP, f, used));
  // 3.5: GCM tag mismatch
  s.begin(35, wrong, V_DEV);
  s.buildStart(out, sizeof out);
  memcpy(buf, V35_SESSRESP, sizeof V35_SESSRESP);
  TEST_ASSERT_EQUAL_INT((int)tuya::Parse::BAD, (int)s.parse(buf, sizeof V35_SESSRESP, f, used));
  // the right key but our nonce differs (another client's handshake): HMAC of the nonce does not match -> 0
  SslCrypto c2;
  c2.ctr = 0x00;
  tuya::Session s2(c2);
  s2.begin(34, key(), V_DEV);
  s2.buildStart(out, sizeof out);
  memcpy(buf, V34_SESSRESP, sizeof V34_SESSRESP);
  TEST_ASSERT_EQUAL_INT((int)tuya::Parse::OK, (int)s2.parse(buf, sizeof V34_SESSRESP, f, used));
  TEST_ASSERT_EQUAL_UINT32(0, s2.onSessResp(f, out, sizeof out));
  TEST_ASSERT_FALSE(s2.ready());
}

void test_resync_on_garbage() {
  SslCrypto c;
  tuya::Session s(c);
  s.begin(33, key(), V_DEV);
  uint8_t buf[600];
  buf[0] = 0x12;
  memcpy(buf + 1, V33_RESP, sizeof V33_RESP);
  tuya::Frame f;
  size_t used;
  TEST_ASSERT_EQUAL_INT((int)tuya::Parse::SKIP, (int)s.parse(buf, sizeof V33_RESP + 1, f, used));
  TEST_ASSERT_EQUAL_INT((int)tuya::Parse::OK, (int)s.parse(buf + 1, sizeof V33_RESP, f, used));
  // an absurd length is a desync, not a frame to wait for
  uint8_t bad[16] = {0, 0, 0x55, 0xAA, 0, 0, 0, 1, 0, 0, 0, 8, 0x7F, 0, 0, 0};
  TEST_ASSERT_EQUAL_INT((int)tuya::Parse::SKIP, (int)s.parse(bad, 16, f, used));
}

// ============================================================================================ data points
void test_decode_dps_and_live_json() {
  MeterLive m;
  TEST_ASSERT_TRUE(meterDecodeDps(V_DPS, strlen(V_DPS), m));
  TEST_ASSERT_TRUE(m.haveCounter);
  TEST_ASSERT_EQUAL_UINT32(30503, m.counter);  // 305.03 kWh, as in the app
  TEST_ASSERT_FLOAT_WITHIN(0.01, 243.4f, m.v);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 0.069f, m.a);
  TEST_ASSERT_FLOAT_WITHIN(0.1, 197.f, m.w);
  TEST_ASSERT_FLOAT_WITHIN(0.1, 196.f, m.rc);
  TEST_ASSERT_EQUAL_INT(1, m.on);
  // 3.4/3.5 put the dps under "data"; a push with only some points keeps the others
  const char* push = "{\"protocol\":4,\"data\":{\"dps\":{\"16\":false}}}";
  TEST_ASSERT_TRUE(meterDecodeDps(push, strlen(push), m));
  TEST_ASSERT_EQUAL_INT(0, m.on);
  TEST_ASSERT_EQUAL_UINT32(30503, m.counter);
  TEST_ASSERT_FALSE(meterDecodeDps("{\"t\":1}", 7, m));
  TEST_ASSERT_FALSE(meterDecodeDps("not json", 8, m));
  m.egm = 1234;
  char b[300];
  JsonOut j(b, sizeof b);
  j.open();
  meterLiveFields(j, m);
  j.close();
  TEST_ASSERT_EQUAL_STRING(
      "{\"mw\":197,\"mv\":243.4,\"mi\":0.069,\"mrc\":196,\"mkwh\":305.03,\"mon\":0,\"egm\":1234}", b);
}

// The owner's meter, 2026-10-24 01:10: its status reply (no phase_a) and the reply to the refresh request.
// Smart Life showed about 0.2 kW, 1.1 A, 244 V, PF 0.66-0.75, 50.4 Hz, 37 °C, leakage 176-196 mA at the time.
void test_decode_owner_meter() {
  const char* st = "{\"1\": 30529, \"9\": 0, \"11\": false, \"12\": false, \"13\": 0, \"15\": 176, \"16\": true, "
                   "\"25\": 37, \"27\": 63, \"32\": 502, \"36\": false, \"49\": true, \"50\": 75, \"101\": \"memory\", "
                   "\"104\": \"\", \"105\": \"////\", \"108\": 0}";
  char wrapped[400];
  snprintf(wrapped, sizeof wrapped, "{\"protocol\":4,\"t\":1792782427,\"data\":{\"dps\":%s}}", st);
  MeterLive m;
  TEST_ASSERT_TRUE(meterDecodeDps(wrapped, strlen(wrapped), m));
  TEST_ASSERT_TRUE(isnan(m.w));  // not in the status reply
  const char* ph = "{\"protocol\":4,\"t\":1792782427,\"data\":{\"dps\":{\"6\":\"CZgABEIAAMk=\"}}}";
  TEST_ASSERT_TRUE(meterDecodeDps(ph, strlen(ph), m));
  char b[300];
  JsonOut j(b, sizeof b);
  j.open();
  meterLiveFields(j, m);
  j.close();
  TEST_ASSERT_EQUAL_STRING("{\"mw\":201,\"mv\":245.6,\"mi\":1.09,\"mpf\":0.75,\"mhz\":50.2,\"mrc\":176,\"mtc\":37,"
                           "\"mkwh\":305.29,\"mon\":1,\"mft\":0}",
                           b);
  // 0.9.1: an alarm bit (3 = leakage) and the reading's age reach the cloud
  const char* al = "{\"dps\":{\"9\":8}}";
  TEST_ASSERT_TRUE(meterDecodeDps(al, strlen(al), m));
  TEST_ASSERT_EQUAL_INT32(8, m.fault);
  m.ageS = 3;
  JsonOut j2(b, sizeof b);
  j2.open();
  meterLiveFields(j2, m);
  j2.close();
  TEST_ASSERT_TRUE(strstr(b, "\"mft\":8,\"mage\":3}") != nullptr);
  // P / (V x I) agrees with data point 50
  TEST_ASSERT_FLOAT_WITHIN(0.01, m.pf, m.w / (m.v * m.a));
}

void test_json_append_int() {
  char b[40] = "{\"egr\":12}";
  TEST_ASSERT_TRUE(jsonAppendInt(b, sizeof b, "egm", 345));
  TEST_ASSERT_EQUAL_STRING("{\"egr\":12,\"egm\":345}", b);
  char e[16] = "{}";
  TEST_ASSERT_TRUE(jsonAppendInt(e, sizeof e, "egm", 1));
  TEST_ASSERT_EQUAL_STRING("{\"egm\":1}", e);
  char f[16] = "{\"a\":1}";
  TEST_ASSERT_FALSE(jsonAppendInt(f, 12, "egm", 123456));  // does not fit: unchanged
  TEST_ASSERT_EQUAL_STRING("{\"a\":1}", f);
}

// ============================================================================================ day accounting
static const int32_t D0 = 20747;  // 2026-10-21
static uint32_t at(int32_t day, int h, int m = 0) { return tu::epochOfLocalMidnight(day) + h * 3600 + m * 60; }

void test_meter_day_full_partial_and_rollover() {
  MeterDay d;
  TEST_ASSERT_FALSE(d.update(at(D0, 10), D0, 30000));  // first reading ever: the install day is partial
  TEST_ASSERT_EQUAL_INT32(-1, d.whFor(D0));
  TEST_ASSERT_FALSE(d.update(at(D0, 23, 59), D0, 30150));
  TEST_ASSERT_TRUE(d.update(at(D0 + 1, 0, 0) + 10, D0 + 1, 30151));  // 10 s after midnight
  TEST_ASSERT_EQUAL_INT32(-1, d.whFor(D0));                          // the install day stays partial
  TEST_ASSERT_EQUAL_INT32(D0, d.state().prevDay);
  TEST_ASSERT_EQUAL_INT32(10, d.whFor(D0 + 1));  // started from the last reading before midnight
  TEST_ASSERT_FALSE(d.update(at(D0 + 1, 20), D0 + 1, 30650));
  TEST_ASSERT_EQUAL_INT32(5000, d.whFor(D0 + 1));
  TEST_ASSERT_FALSE(d.update(at(D0 + 1, 23, 59), D0 + 1, 30651));
  TEST_ASSERT_TRUE(d.update(at(D0 + 2, 0, 1), D0 + 2, 30653));
  TEST_ASSERT_EQUAL_INT32(5010, d.whFor(D0 + 1));  // finished day: kept for its final daily write
  TEST_ASSERT_EQUAL_INT32(20, d.whFor(D0 + 2));
  TEST_ASSERT_EQUAL_INT32(-1, d.whFor(D0 + 3));
}

void test_meter_day_gaps_and_glitches() {
  MeterDay d;
  d.update(at(D0, 12), D0, 1000);
  d.update(at(D0, 21), D0, 1100);                // last reading 3 h before midnight (logger off)
  TEST_ASSERT_TRUE(d.update(at(D0 + 1, 1), D0 + 1, 1150));
  TEST_ASSERT_EQUAL_INT32(-1, d.whFor(D0 + 1));  // more than 2 h gap: partial, the app uses the estimate
  // a full day, then the counter goes down (meter cleared or replaced): partial from there
  d.update(at(D0 + 1, 23, 59), D0 + 1, 1200);
  d.update(at(D0 + 2, 0, 0) + 5, D0 + 2, 1200);
  TEST_ASSERT_EQUAL_INT32(0, d.whFor(D0 + 2));
  d.update(at(D0 + 2, 8), D0 + 2, 50);
  TEST_ASSERT_EQUAL_INT32(-1, d.whFor(D0 + 2));
  TEST_ASSERT_EQUAL_UINT32(1, d.resets());
  // implausible jump: more than 16 kWh/h
  MeterDay e;
  e.update(at(D0, 23), D0, 1000);
  e.update(at(D0 + 1, 0) + 5, D0 + 1, 1001);
  e.update(at(D0 + 1, 1), D0 + 1, 1100);  // 1 kWh in an hour: fine
  TEST_ASSERT_EQUAL_INT32(1000, e.whFor(D0 + 1));
  e.update(at(D0 + 1, 1, 1), D0 + 1, 9000);  // 79 kWh in a minute
  TEST_ASSERT_EQUAL_INT32(-1, e.whFor(D0 + 1));
  // a whole day skipped: the new day is partial, the old one closes with what it had
  MeterDay g;
  g.update(at(D0, 23, 50), D0, 100);
  g.update(at(D0 + 1, 0) + 5, D0 + 1, 101);
  g.update(at(D0 + 1, 12), D0 + 1, 200);
  TEST_ASSERT_TRUE(g.update(at(D0 + 3, 9), D0 + 3, 900));
  TEST_ASSERT_EQUAL_INT32(1000, g.whFor(D0 + 1));
  TEST_ASSERT_EQUAL_INT32(-1, g.whFor(D0 + 3));
  // a reboot keeps the state (NVS blob)
  MeterDay h;
  h.load(g.state());
  TEST_ASSERT_EQUAL_INT32(1000, h.whFor(D0 + 1));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_crc32_check_value);
  RUN_TEST(test_crypto_self_test);
  RUN_TEST(test_v33_query_and_responses);
  RUN_TEST(test_v34_handshake_and_query);
  RUN_TEST(test_v35_handshake_and_query);
  RUN_TEST(test_wrong_key_is_rejected);
  RUN_TEST(test_resync_on_garbage);
  RUN_TEST(test_decode_dps_and_live_json);
  RUN_TEST(test_decode_owner_meter);
  RUN_TEST(test_json_append_int);
  RUN_TEST(test_meter_day_full_partial_and_rollover);
  RUN_TEST(test_meter_day_gaps_and_glitches);
  return UNITY_END();
}
