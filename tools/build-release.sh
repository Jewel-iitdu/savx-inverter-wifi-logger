#!/usr/bin/env bash
# Build the firmware release images without local paths in them.
#   tools/build-release.sh [out-dir]            (default: release/v<FW_VERSION>)
# The compiler keeps source paths (__FILE__) for asserts and logs; on a Mac they contain the user name. The paths are
# rewritten to /pio (PlatformIO core) and /src (this project), and the run fails if any local path is left.
# Outputs:
#   savx-logger-<ver>-esp32s3-n16r8-factory.bin  first install over USB: write at 0x0 (bootloader + partitions + app)
#   savx-logger-<ver>-ota.bin                    updates: upload at http://<logger-ip>/update ("Firmware")
#   SHA256SUMS.txt
# Env: PIO (default: pio), PLATFORMIO_CORE_DIR (default: ~/.platformio).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PIO="${PIO:-pio}"
CORE="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
cd "$ROOT/firmware"
VER="$(grep -o 'FW_VERSION "[0-9.]*"' include/config.h | grep -o '[0-9][0-9.]*')"
# Version rule A.B.C: B and C are 0-9 (after x.y.9 comes x.(y+1).0, after x.9.9 comes (x+1).0.0)
[[ "$VER" =~ ^[0-9]+\.[0-9]\.[0-9]$ ]] || { echo "FW_VERSION $VER breaks the A.B.C rule (B and C 0-9)" >&2; exit 1; }
OUT="${1:-$ROOT/release/v$VER}"

map() { printf -- '-ffile-prefix-map=%s=%s -fmacro-prefix-map=%s=%s ' "$1" "$2" "$1" "$2"; }
FLAGS=""
for p in "$CORE" "$(cd "$CORE" && pwd -P)"; do FLAGS+="$(map "$p" /pio)$(map "/$p" /pio)"; done
for p in "$ROOT" "$(cd "$ROOT" && pwd -P)"; do FLAGS+="$(map "$p" /src)$(map "/$p" /src)"; done
export PLATFORMIO_BUILD_FLAGS="$FLAGS"

"$PIO" run -e savx -t clean > /dev/null
"$PIO" run -e savx

mkdir -p "$OUT"
cp .pio/build/savx/firmware.factory.bin "$OUT/savx-logger-$VER-esp32s3-n16r8-factory.bin"
cp .pio/build/savx/firmware.bin "$OUT/savx-logger-$VER-ota.bin"
( cd "$OUT" && shasum -a 256 "savx-logger-$VER"-*.bin > SHA256SUMS.txt )

if strings -n 4 "$OUT"/savx-logger-"$VER"-*.bin | grep -E '/(Users|home|private|tmp|var/folders)/'; then
  echo "ERROR: a local path is still inside the images (see above)" >&2
  exit 1
fi
echo "release $VER ready in $OUT"
cat "$OUT/SHA256SUMS.txt"
