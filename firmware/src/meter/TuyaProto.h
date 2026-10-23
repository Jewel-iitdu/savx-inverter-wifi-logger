// TuyaProto.h - the Tuya LAN protocol (port 6668), versions 3.3 / 3.4 / 3.5, for the grid meter (0.9.0).
// Host-portable. Read-only by design: only the session-key handshake and the status query are implemented, there
// is no command that could switch the breaker or change a setting (the Smart Life app stays the only controller).
// Framing and key handshake follow tinytuya 1.20 (core/message_helper.py, XenonDevice.py):
//   3.3  000055AA seq cmd len | [retcode] AES-128-ECB(local key, PKCS7) | CRC32 | 0000AA55
//   3.4  same frame, HMAC-SHA256 instead of CRC32, AES-ECB with the session key after the handshake
//   3.5  00006699 0000 seq cmd len | iv(12) AES-128-GCM(key, aad = header[4..18]) tag(16) | 00009966
// Frames from the device carry a 4-byte return code (in clear for 55AA, first in the plaintext for 6699), and a
// pushed payload may start with the version header "3.x" + 12 zero bytes.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace tuya {

// Crypto primitives: mbedtls on the device (MeterCrypto.cpp), OpenSSL in the host tests.
struct Crypto {
  virtual ~Crypto() {}
  virtual bool ecb(bool enc, const uint8_t key[16], const uint8_t* in, size_t n, uint8_t* out) = 0;  // n % 16 == 0
  virtual bool hmac(const uint8_t key[16], const uint8_t* d, size_t n, uint8_t out[32]) = 0;
  virtual bool gcmEnc(const uint8_t key[16], const uint8_t iv[12], const uint8_t* aad, size_t aadN, const uint8_t* in,
                      size_t n, uint8_t* out, uint8_t tag[16]) = 0;
  virtual bool gcmDec(const uint8_t key[16], const uint8_t iv[12], const uint8_t* aad, size_t aadN, const uint8_t* in,
                      size_t n, const uint8_t tag[16], uint8_t* out) = 0;
  virtual void random(uint8_t* out, size_t n) = 0;
};

uint32_t crc32(const uint8_t* d, size_t n);  // zlib CRC-32
// Known-answer check of the four primitives (AES-ECB, HMAC-SHA256, AES-GCM both ways); the device runs it once
// before it uses mbedtls for the meter, the host tests run it against OpenSSL.
bool selfTest(Crypto& c);

constexpr uint32_t CMD_SESS_START = 3, CMD_SESS_RESP = 4, CMD_SESS_FINISH = 5, CMD_STATUS = 8, CMD_DP_QUERY = 10,
                   CMD_DP_QUERY_NEW = 16, CMD_UPDATEDPS = 18;
constexpr size_t MAX_FRAME = 1400;

struct Frame {
  uint32_t seq = 0, cmd = 0, retcode = 0;
  const uint8_t* payload = nullptr;  // decrypted, padding and version header removed (points into the parse buffer)
  size_t n = 0;
};

enum class Parse : int8_t { MORE = 0, OK = 1, BAD = -1, SKIP = -2 };

class Session {
 public:
  explicit Session(Crypto& c) : c_(c) {}

  // ver = 33, 34 or 35. devId is only used by the 3.3 query.
  void begin(int ver, const uint8_t localKey[16], const char* devId);
  int version() const { return ver_; }
  bool needsHandshake() const { return ver_ >= 34; }
  bool ready() const { return ver_ == 33 || keyed_; }

  size_t buildStart(uint8_t* out, size_t cap);  // 3.4/3.5 step 1 (fresh random nonce)
  // Step 2 -> 3: checks the device's HMAC of our nonce, builds the finish frame and switches to the session key.
  // 0 = the device's answer did not match (wrong key or not a SESS_RESP).
  size_t onSessResp(const Frame& f, uint8_t* out, size_t cap);
  size_t buildQuery(uint8_t* out, size_t cap, uint32_t epoch);
  // Ask the device to report one data point now (tinytuya updatedps). Still read-only: the owner's breaker leaves
  // phase_a (V, A, W) out of its status reply and sends it only on this request or every ~15 s on its own.
  size_t buildRefresh(uint8_t* out, size_t cap, int dp);

  // One frame at buf[0]. OK: consumed = frame length and f is filled (buf is decrypted in place). MORE: need
  // `need` bytes in total. BAD: a complete frame that failed its CRC / HMAC / tag or decryption (consumed bytes
  // can be dropped). SKIP: no frame prefix at buf[0] (drop 1 byte and retry).
  Parse parse(uint8_t* buf, size_t n, Frame& f, size_t& consumed);

 private:
  size_t pack(uint32_t cmd, const uint8_t* plain, size_t n, uint8_t* out, size_t cap);
  Crypto& c_;
  int ver_ = 33;
  uint8_t real_[16] = {0}, key_[16] = {0}, nonce_[16] = {0};
  char devId_[32] = "";
  uint32_t seq_ = 1;
  bool keyed_ = false;
};

}  // namespace tuya
