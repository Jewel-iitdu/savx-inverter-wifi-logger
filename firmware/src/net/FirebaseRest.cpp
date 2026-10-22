// FirebaseRest.cpp - see FirebaseRest.h.
#include "net/FirebaseRest.h"

#include "config.h"
#include "hal/ArduinoHal.h"
#include "model/JsonOut.h"
#include "net/UplinkJson.h"
#include "roots.h"

// token + scratch (uplinkTask only), in PSRAM
struct AuthBufs {
  char idToken[2048];
  upj::AuthResult auth;
  char resp[4096];
  char path[2400];
  char form[1200];
  char body[400];
};
static AuthBufs* B = nullptr;
#define s_idToken (B->idToken)
#define s_auth (B->auth)
#define s_authResp (B->resp)
#define s_path (B->path)

FirebaseRest::FirebaseRest() : db_(roots::GOOGLE), auth_(roots::GOOGLE) {}

bool FirebaseRest::configure(const String& apiKey, const String& dbUrl, const String& email, const String& pass,
                             const String& refreshToken, SaveTokenFn save) {
  configured_ = false;
  haveToken_ = false;
  save_ = save;
  if (!B) B = psramNew<AuthBufs>();
  if (!B) {
    snprintf(err_, sizeof err_, "out of memory");
    return false;
  }
  apiKey_ = apiKey;
  email_ = email;
  pass_ = pass;
  rtok_ = refreshToken;
  holdUntilMs_ = 0;
  authBackoff_.ok();
  char host[96];
  if (!apiKey.length() || !dbUrl.length() || !email.length() || !pass.length()) {
    snprintf(err_, sizeof err_, "cloud credentials incomplete (portal: API key, DB URL, e-mail, password)");
    return false;
  }
  if (!upj::dbHostFromUrl(dbUrl.c_str(), host, sizeof host)) {
    snprintf(err_, sizeof err_, "DB URL must be https://<db>.<region>.firebasedatabase.app");
    return false;
  }
  snprintf(dbHost_, sizeof dbHost_, "%s", host);
  db_.setHost(dbHost_);
  err_[0] = 0;
  configured_ = true;
  return true;
}

FirebaseRest::AuthRes FirebaseRest::finishAuth(const HttpResult& r, bool isRefresh) {
  if (r.status == 200) {
    bool ok = isRefresh ? upj::parseRefresh(s_authResp, r.bodyLen, s_auth) : upj::parseSignIn(s_authResp, r.bodyLen, s_auth);
    if (!ok) {
      snprintf(err_, sizeof err_, "%s: unexpected response", isRefresh ? "token refresh" : "sign-in");
      return AuthRes::TRANSIENT;
    }
    snprintf(s_idToken, sizeof s_idToken, "%s", s_auth.idToken);
    uint32_t life = s_auth.expiresIn > cfg::TOKEN_REFRESH_BEFORE_S + 60 ? s_auth.expiresIn : 3600;
    expiryMs_ = millis() + (life - cfg::TOKEN_REFRESH_BEFORE_S) * 1000UL;
    haveToken_ = true;
    bool changed = rtok_ != s_auth.refreshToken || strcmp(uid_, s_auth.uid) != 0;
    rtok_ = s_auth.refreshToken;
    if (s_auth.uid[0]) snprintf(uid_, sizeof uid_, "%s", s_auth.uid);
    if (changed && save_) save_(rtok_.c_str(), uid_);
    err_[0] = 0;
    return AuthRes::OK;
  }
  if (r.status < 0) {
    snprintf(err_, sizeof err_, "%s", r.err);
    return AuthRes::TRANSIENT;
  }
  char m[64] = "";
  upj::parseApiError(s_authResp, r.bodyLen, m, sizeof m);
  snprintf(err_, sizeof err_, "%s HTTP %d %s", isRefresh ? "token refresh" : "sign-in", r.status, m);
  if (r.status >= 500 || r.status == 429) return AuthRes::TRANSIENT;
  return AuthRes::PERMANENT;  // 400: bad/expired refresh token, wrong password, disabled user, wrong API key ...
}

FirebaseRest::AuthRes FirebaseRest::refresh() {
  auth_.setHost("securetoken.googleapis.com");
  snprintf(s_path, sizeof s_path, "/v1/token?key=%s", apiKey_.c_str());
  char* form = B->form;
  char enc[1100];
  if (!upj::urlEncode(rtok_.c_str(), enc, sizeof enc)) return AuthRes::PERMANENT;
  int n = snprintf(form, sizeof B->form, "grant_type=refresh_token&refresh_token=%s", enc);
  HttpResult r = auth_.request("POST", s_path, "application/x-www-form-urlencoded", form, (size_t)n, s_authResp,
                               sizeof s_authResp, false);
  return finishAuth(r, true);
}

FirebaseRest::AuthRes FirebaseRest::signIn() {
  auth_.setHost("identitytoolkit.googleapis.com");
  snprintf(s_path, sizeof s_path, "/v1/accounts:signInWithPassword?key=%s", apiKey_.c_str());
  char* body = B->body;
  JsonOut j(body, sizeof B->body);
  j.open();
  j.s("email", email_.c_str());
  j.s("password", pass_.c_str());
  j.b("returnSecureToken", true);
  j.close();
  if (!j.ok()) return AuthRes::PERMANENT;
  HttpResult r = auth_.request("POST", s_path, "application/json", body, j.len(), s_authResp, sizeof s_authResp, false);
  memset(body, 0, sizeof B->body);  // the password does not linger in RAM
  return finishAuth(r, false);
}

bool FirebaseRest::ensureAuth(uint32_t nowMs) {
  if (!configured_ || !B) return false;
  if (haveToken_ && (int32_t)(nowMs - expiryMs_) < 0) return true;
  bool stillValid = haveToken_ && (int32_t)(nowMs - (expiryMs_ + cfg::TOKEN_REFRESH_BEFORE_S * 1000UL)) < 0;
  if (!authBackoff_.ready(nowMs) || (holdUntilMs_ && (int32_t)(nowMs - holdUntilMs_) < 0)) return stillValid;
  holdUntilMs_ = 0;
  if (rtok_.length()) {
    AuthRes a = refresh();
    if (a == AuthRes::OK) {
      authBackoff_.ok();
      return true;
    }
    if (a == AuthRes::TRANSIENT) {
      authBackoff_.fail(nowMs);
      return stillValid;
    }
    rtok_ = "";  // dead refresh token: full sign-in
    if (save_) save_("", uid_);
  }
  AuthRes a = signIn();
  if (a == AuthRes::OK) {
    authBackoff_.ok();
    return true;
  }
  haveToken_ = false;
  if (a == AuthRes::TRANSIENT) authBackoff_.fail(nowMs);
  else holdUntilMs_ = nowMs + 30UL * 60000UL;  // wrong credentials: wait for the owner (portal) instead of hammering
  return false;
}

FirebaseRest::Res FirebaseRest::rtdb(const char* method, const char* path, const char* body, size_t len, char* resp,
                                     size_t cap, bool silent, const char* extraQuery) {
  Res out;
  for (int attempt = 0; attempt < 2; attempt++) {
    if (!ensureAuth(millis())) {
      out.kind = Kind::NOAUTH;
      return out;
    }
    if (!upj::rtdbPath(path, s_idToken, silent, extraQuery, s_path, sizeof s_path)) {
      out.kind = Kind::CLIENT;
      snprintf(err_, sizeof err_, "path too long");
      return out;
    }
    HttpResult r = db_.request(method, s_path, "application/json", body, len, resp, cap, true);
    out.http = r.status;
    out.len = r.bodyLen;
    out.truncated = r.truncated;
    if (r.status < 0) {
      out.kind = Kind::NET;
      snprintf(err_, sizeof err_, "%s", r.err);
      return out;
    }
    if (r.status >= 200 && r.status < 300) {
      out.kind = Kind::OK;
      return out;
    }
    char m[64] = "";
    if (resp && r.bodyLen) upj::parseApiError(resp, r.bodyLen, m, sizeof m);
    snprintf(err_, sizeof err_, "%s %s: HTTP %d %s", method, path[0] ? path : "/", r.status, m);
    if (r.status == 401 && attempt == 0) {  // expired / revoked token: refresh once and retry
      haveToken_ = false;
      continue;
    }
    out.kind = r.status == 401 || r.status == 403 ? Kind::DENIED
               : r.status == 429                  ? Kind::RATE
               : r.status >= 500                  ? Kind::SERVER
                                                  : Kind::CLIENT;
    return out;
  }
  return out;
}
