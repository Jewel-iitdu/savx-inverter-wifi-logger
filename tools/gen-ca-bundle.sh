#!/usr/bin/env bash
# Regenerate firmware/x509_crt_bundle (Mozilla root CAs, ESP-IDF bundle format) for the remote firmware update's
# HTTPS download from GitHub (net/RemoteOta.cpp). Uses certifi's cacert.pem and the framework's gen_crt_bundle.py.
# Usage: tools/gen-ca-bundle.sh   (PLATFORMIO_CORE_DIR as for the build; needs python3 with cryptography + certifi)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
core="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
py="${PYTHON:-$root/tools/meter/.venv/bin/python}"
gen="$core/packages/framework-arduinoespressif32/tools/gen_crt_bundle.py"
ca="$("$py" -c 'import certifi; print(certifi.where())')"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
(cd "$tmp" && "$py" "$gen" --input "$ca" -q)
cp "$tmp/x509_crt_bundle" "$root/firmware/x509_crt_bundle"
echo "firmware/x509_crt_bundle: $(wc -c < "$root/firmware/x509_crt_bundle") bytes from $ca"
