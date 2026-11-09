#!/usr/bin/env bash
# Signs a firmware image and prints the OTA manifest consumed by the device
# (MQTT command {"action":"OTA_UPDATE","ota":{...}}).
#
#   tools/ota_sign.sh <firmware.bin> <signing_key.pem> <version> <https-url> <hwRev>
set -euo pipefail
BIN=${1:?firmware.bin}; KEY=${2:?key}; VER=${3:?version}; URL=${4:?url}; HW=${5:?hwRev}
case "$URL" in https://*) ;; *) echo "URL must be https://" >&2; exit 1;; esac
SHA=$(openssl dgst -sha256 -r "$BIN" | cut -d' ' -f1)
SIG=$(openssl dgst -sha256 -sign "$KEY" "$BIN" | openssl base64 -A)
SIZE=$(stat -c%s "$BIN" 2>/dev/null || stat -f%z "$BIN")
# Self-check the signature with the matching public key before publishing.
openssl ec -in "$KEY" -pubout 2>/dev/null > /tmp/.wt_pub.$$
echo "$SIG" | openssl base64 -d -A > /tmp/.wt_sig.$$
openssl dgst -sha256 -verify /tmp/.wt_pub.$$ -signature /tmp/.wt_sig.$$ "$BIN" >/dev/null
rm -f /tmp/.wt_pub.$$ /tmp/.wt_sig.$$
cat <<JSON
{"version":"$VER","url":"$URL","sha256":"$SHA","signature":"$SIG","size":$SIZE,"hwRev":"$HW","stopMotorIfRunning":true}
JSON
