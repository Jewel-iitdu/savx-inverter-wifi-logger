#!/usr/bin/env bash
# Get the grid meter's local key from Smart Life (QR login) and check it on the LAN. Read-only; see get_key.py.
# Usage: tools/meter/get-key.sh [meter LAN IP; default: every host on the LAN with port 6668 open]
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
if [ ! -x "$here/.venv/bin/python" ]; then
  python3 -m venv "$here/.venv"
  "$here/.venv/bin/pip" install -q tuya-device-sharing-sdk tinytuya "qrcode[pil]"
fi
exec "$here/.venv/bin/python" -I "$here/get_key.py" "$@"
