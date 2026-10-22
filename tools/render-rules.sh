#!/bin/sh
# Render firebase/database.rules.json with the real Auth UIDs into .deploy/ (gitignored).
# The committed rules keep the OWNER_UID / DEVICE_UID / MAILER_UID placeholders (firebase/README.md section 3).
#
#   OWNER_UID=<owner uid> DEVICE_UID=<device uid> MAILER_UID=<mailer uid> sh tools/render-rules.sh
#   cd .deploy && npx --yes firebase-tools@15.32.1 deploy --only database --project <project-id>
#
# MAILER_UID is the Auth user of the Apps Script e-mail mailer (research/r8-email-alerts.md section 7, step 2).
# No mailer yet? Pass MAILER_UID=none: its grants are then pinned to a UID that no account has.
# research/r7-web-hosting.md section 3.6 (extended with MAILER_UID for research/r8-email-alerts.md section 3.11)
set -eu
: "${OWNER_UID:?set OWNER_UID (Firebase console > Authentication > Users > User UID)}"
: "${DEVICE_UID:?set DEVICE_UID (Auth user of the logger)}"
: "${MAILER_UID:?set MAILER_UID (Auth user of the e-mail mailer), or MAILER_UID=none if there is no mailer yet}"
if [ "$MAILER_UID" = none ]; then
  MAILER_UID=mailerNotConfigured
  echo "render-rules: MAILER_UID=none: the mailer grants match no account (e-mail alerts stay queued)" >&2
fi
for uid in "$OWNER_UID" "$DEVICE_UID" "$MAILER_UID"; do
  case "$uid" in
    *[!A-Za-z0-9]*) echo "render-rules: a UID may only contain letters and digits: '$uid'" >&2; exit 1 ;;
  esac
done
if [ "$OWNER_UID" = "$DEVICE_UID" ] || [ "$OWNER_UID" = "$MAILER_UID" ] || [ "$DEVICE_UID" = "$MAILER_UID" ]; then
  echo "render-rules: OWNER_UID, DEVICE_UID and MAILER_UID must all differ" >&2; exit 1
fi

cd "$(dirname "$0")/.."
src=firebase/database.rules.json
out=.deploy
if ! grep -q OWNER_UID "$src" || ! grep -q DEVICE_UID "$src" || ! grep -q MAILER_UID "$src"; then
  echo "render-rules: $src has no placeholders (real UIDs committed?)" >&2; exit 1
fi
mkdir -p "$out"
sed -e "s/OWNER_UID/${OWNER_UID}/g" -e "s/DEVICE_UID/${DEVICE_UID}/g" -e "s/MAILER_UID/${MAILER_UID}/g" "$src" > "$out/database.rules.json"
if grep -q 'OWNER_UID\|DEVICE_UID\|MAILER_UID' "$out/database.rules.json"; then echo "render-rules: placeholder left" >&2; exit 1; fi
python3 -m json.tool "$out/database.rules.json" > /dev/null
printf '{\n  "database": { "rules": "database.rules.json" }\n}\n' > "$out/firebase.json"
echo "render-rules: wrote $out/database.rules.json and $out/firebase.json"
