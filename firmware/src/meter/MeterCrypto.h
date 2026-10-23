// MeterCrypto.h - tuya::Crypto on mbedtls (ESP32-S3 hardware AES/SHA) and the hardware RNG. Device only.
#pragma once
#include "meter/TuyaProto.h"

class MbedCrypto : public tuya::Crypto {
 public:
  bool ecb(bool enc, const uint8_t key[16], const uint8_t* in, size_t n, uint8_t* out) override;
  bool hmac(const uint8_t key[16], const uint8_t* d, size_t n, uint8_t out[32]) override;
  bool gcmEnc(const uint8_t key[16], const uint8_t iv[12], const uint8_t* aad, size_t aadN, const uint8_t* in,
              size_t n, uint8_t* out, uint8_t tag[16]) override;
  bool gcmDec(const uint8_t key[16], const uint8_t iv[12], const uint8_t* aad, size_t aadN, const uint8_t* in,
              size_t n, const uint8_t tag[16], uint8_t* out) override;
  void random(uint8_t* out, size_t n) override;

 private:
  uint8_t tmp_[tuya::MAX_FRAME];  // GCM never works in place here (the parser decrypts in its own buffer)
};
