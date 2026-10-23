// MeterCrypto.cpp - see MeterCrypto.h.
#include "meter/MeterCrypto.h"

#include <esp_random.h>
#include <mbedtls/aes.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <string.h>

bool MbedCrypto::ecb(bool enc, const uint8_t key[16], const uint8_t* in, size_t n, uint8_t* out) {
  if (n % 16) return false;
  mbedtls_aes_context a;
  mbedtls_aes_init(&a);
  bool ok = (enc ? mbedtls_aes_setkey_enc(&a, key, 128) : mbedtls_aes_setkey_dec(&a, key, 128)) == 0;
  uint8_t blk[16];
  for (size_t i = 0; ok && i < n; i += 16) {
    ok = mbedtls_aes_crypt_ecb(&a, enc ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT, in + i, blk) == 0;
    memcpy(out + i, blk, 16);
  }
  mbedtls_aes_free(&a);
  return ok;
}

bool MbedCrypto::hmac(const uint8_t key[16], const uint8_t* d, size_t n, uint8_t out[32]) {
  return mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, 16, d, n, out) == 0;
}

bool MbedCrypto::gcmEnc(const uint8_t key[16], const uint8_t iv[12], const uint8_t* aad, size_t aadN,
                        const uint8_t* in, size_t n, uint8_t* out, uint8_t tag[16]) {
  if (n > sizeof tmp_) return false;
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  bool ok = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 128) == 0 &&
            mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, n, iv, 12, aad, aadN, in, tmp_, 16, tag) == 0;
  mbedtls_gcm_free(&g);
  if (ok) memcpy(out, tmp_, n);
  return ok;
}

bool MbedCrypto::gcmDec(const uint8_t key[16], const uint8_t iv[12], const uint8_t* aad, size_t aadN,
                        const uint8_t* in, size_t n, const uint8_t tag[16], uint8_t* out) {
  if (n > sizeof tmp_) return false;
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  bool ok = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 128) == 0 &&
            mbedtls_gcm_auth_decrypt(&g, n, iv, 12, aad, aadN, tag, 16, in, tmp_) == 0;
  mbedtls_gcm_free(&g);
  if (ok) memcpy(out, tmp_, n);
  return ok;
}

void MbedCrypto::random(uint8_t* out, size_t n) { esp_fill_random(out, n); }
