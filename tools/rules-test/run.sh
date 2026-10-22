#!/bin/sh
# Tests of firebase/database.rules.json in the local Realtime Database EMULATOR:
#   1. rtest.mjs  - allow/deny cases (owner, device, mailer, stranger);
#   2. parity.mjs - the PWA's client-side validators accept exactly what the rules accept
#                   (needs web/node_modules for esbuild; skipped with a note otherwise).
# No Firebase project, no login and no deploy: the "demo-" project id keeps everything local.
# Needs Node 20+ and Java 11+ (the emulator is a JAR that firebase-tools downloads once to
# ~/.cache/firebase/emulators).
#
#   sh tools/rules-test/run.sh            # ends with "N passed, 0 failed" and "parity: N agree, 0 disagree"
#
# FIREBASE_TOOLS overrides the CLI command (default: npx --yes firebase-tools@15.32.1).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/savx-rules-test.XXXXXX")
trap 'rm -rf "$work"' EXIT INT TERM

# The same placeholders tools/render-rules.sh fills in, with the test UIDs rtest.mjs signs as.
sed -e 's/OWNER_UID/owner1/g' -e 's/DEVICE_UID/device1/g' -e 's/MAILER_UID/mailer1/g' \
  "$root/firebase/database.rules.json" > "$work/database.rules.json"
cat > "$work/firebase.json" <<'JSON'
{
  "database": { "rules": "database.rules.json" },
  "emulators": { "database": { "port": 9123 }, "ui": { "enabled": false }, "singleProjectMode": true }
}
JSON
cp "$here/rtest.mjs" "$here/parity.mjs" "$work/"

tests="node rtest.mjs"
esbuild="$root/web/node_modules/.bin/esbuild"
if [ -x "$esbuild" ]; then
  "$esbuild" "$root/web/src/data/alerts.ts" --bundle --format=esm --platform=node --log-level=warning --outfile="$work/alerts.mjs"
  tests="node rtest.mjs && node parity.mjs"
else
  echo "rules-test: web/node_modules missing (run npm ci in web/): skipping the validator parity fuzz" >&2
fi

cd "$work"
# shellcheck disable=SC2086 # FIREBASE_TOOLS is a command line on purpose
${FIREBASE_TOOLS:-npx --yes firebase-tools@15.32.1} --non-interactive \
  emulators:exec --only database --project demo-savx "$tests"
