# Solar Logger for Android

A small Android app (Capacitor 8) that opens the dashboard, https://solar.<your-subdomain>.workers.dev, full screen.

- **Sign in once.** Firebase Auth keeps the session in this app's own WebView storage (IndexedDB). It survives
  closing the app and restarting the phone. It ends only on **Settings → Sign out**, after clearing the app's
  storage, or after uninstalling.
- **Always up to date.** The app loads the live site, so every `wrangler deploy` of `web/` reaches the app at once.
  A new APK is needed only for app-level changes (icon, name, this shell's code).
- **Back button** goes back through the dashboard's tabs, then leaves the app.

## Install on a phone

1. Copy `release/solar-logger-<version>.apk` to the phone (USB, Google Drive, Telegram "Saved messages", e-mail to yourself, Quick Share…).
2. Open it on the phone. Android asks to allow installs from that app (Files, Drive, Chrome…): allow it once.
3. Open **Solar Logger**, sign in with the dashboard e-mail and password. Done: it stays signed in.

Updates: install a newer APK over the old one (same signing key). The sign-in is kept.

## Build

Needs Node 24 (nvm), JDK 21 and the Android SDK (here: Homebrew `android-commandlinetools`, licences already accepted).

```bash
cd android-app
export JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home
export ANDROID_HOME=/opt/homebrew/share/android-commandlinetools
npm install
npm run apk     # cap sync android + ./gradlew assembleRelease
# -> android/app/build/outputs/apk/release/app-release.apk
```

Bump `versionCode` / `versionName` in `android/app/build.gradle` for every APK you install over an older one.

## Signing key (back it up)

`keystore/solar-release.jks` and `keystore.properties` (passwords) are created locally and git-ignored.
**Keep a copy somewhere safe.** Without them a new APK cannot be installed over the old one: you would have to
uninstall the app (losing the sign-in) and install again.

## Files

| Path | What |
|---|---|
| `capacitor.config.json` | App id `io.github.savxlogger.solar`, name, `server.url` (the dashboard), system-bar insets (`css`: the web app's `env(safe-area-inset-*)` padding works) |
| `www/index.html` | Fallback page, only shown if the dashboard cannot be loaded |
| `android/app/src/main/java/io/github/savxlogger/solar/MainActivity.java` | Back-button handling |
| `android/app/src/main/res/mipmap-*` | Launcher icons (from `web/public/icons`) |
| `android/app/build.gradle` | Version and release signing (reads `../keystore.properties`) |
