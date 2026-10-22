// FirebaseRest.h - Firebase Auth (email/password) + Realtime Database REST for the device user (d1 §4.8, V2 F10/F11).
//   sign-in : POST https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=<API_KEY>
//             {"email","password","returnSecureToken":true} -> idToken, refreshToken, expiresIn, localId
//   refresh : POST https://securetoken.googleapis.com/v1/token?key=<API_KEY>  (form grant_type=refresh_token&...)
//             -> id_token, refresh_token, expires_in, user_id   (5 min before expiry; on any RTDB 401 once)
//   RTDB    : <METHOD> https://<db host>/devices/<id>/<path>.json?auth=<ID_TOKEN>[&print=silent]
// One keep-alive TLS client for RTDB (every new TLS session is ~3.5 KB of billed download, V2 F3), a separate
// one-shot client for the token endpoints (about once an hour). The refresh token is persisted in NVS cloud/rtok and
// dies on a password change (V2 F11): a refresh error then falls back to a full sign-in. Credential errors pause
// sign-in attempts for 30 min (no lockout from TOO_MANY_ATTEMPTS). Never logs tokens or URLs with `auth=`.
#pragma once
#include <Arduino.h>

#include "net/Budget.h"
#include "net/HttpsClient.h"

class FirebaseRest {
 public:
  enum class Kind : uint8_t { OK, NOAUTH, DENIED, RATE, SERVER, NET, CLIENT };
  struct Res {
    int http = 0;
    Kind kind = Kind::NET;
    size_t len = 0;
    bool truncated = false;
  };
  typedef void (*SaveTokenFn)(const char* refreshToken, const char* uid);

  FirebaseRest();
  // returns false (and lastError) when something required is missing or the DB URL is not a Firebase RTDB host
  bool configure(const String& apiKey, const String& dbUrl, const String& email, const String& pass,
                 const String& refreshToken, SaveTokenFn save);
  bool configured() const { return configured_; }
  bool ensureAuth(uint32_t nowMs);  // a valid ID token (refresh / sign-in as needed, with back-off)
  bool authOk() const { return haveToken_; }
  void forceReauth() { haveToken_ = false; }
  void drop() { db_.stop(); }

  Res rtdb(const char* method, const char* path, const char* body, size_t len, char* resp, size_t cap, bool silent = true,
           const char* extraQuery = nullptr);

  const char* lastError() const { return err_; }
  const char* uid() const { return uid_; }
  uint32_t dbHandshakes() const { return db_.handshakes(); }
  uint32_t dbRequests() const { return db_.requests(); }
  uint32_t lastConnectOkMs() const { return db_.lastConnectOkMs(); }
  bool everConnected() const { return db_.everConnected() || auth_.everConnected(); }

 private:
  enum class AuthRes : uint8_t { OK, TRANSIENT, PERMANENT };
  AuthRes refresh();
  AuthRes signIn();
  AuthRes finishAuth(const HttpResult& r, bool isRefresh);

  HttpsClient db_, auth_;
  String apiKey_, email_, pass_, rtok_;
  char dbHost_[96] = "";
  char uid_[64] = "";
  bool configured_ = false, haveToken_ = false;
  uint32_t expiryMs_ = 0, holdUntilMs_ = 0;
  Backoff authBackoff_;
  SaveTokenFn save_ = nullptr;
  char err_[96] = "";
};
