#!/usr/bin/env bash
# Build and run tools/meter/probe.cpp (the firmware's meter code on the Mac, OpenSSL crypto) against the meter.
# Usage (repo root): tools/meter/probe.sh [ip] [33|34|35]
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
ssl=/opt/homebrew/opt/openssl@3
aj="$(find "$root/firmware/.pio/libdeps/native" -maxdepth 2 -name ArduinoJson -type d | head -1)/src"
out="${TMPDIR:-/tmp}/meter-probe"
c++ -std=gnu++17 -O1 -DNATIVE_BUILD -I"$root/firmware/include" -I"$root/firmware/src" -I"$aj" -I"$ssl/include" \
  "$root/tools/meter/probe.cpp" "$root/firmware/src/meter/TuyaProto.cpp" "$root/firmware/src/meter/MeterLive.cpp" \
  -L"$ssl/lib" -lcrypto -o "$out"
cd "$root" && exec "$out" "$@"
