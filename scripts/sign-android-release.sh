#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
version="${1:?Supply a release tag such as v1.0.0}"
case "$version" in
  v[0-9]* ) ;;
  * ) echo 'A release tag must start with v followed by a number.' >&2; exit 2 ;;
esac
for name in APK_KEYSTORE_B64 APK_KEYSTORE_PASSWORD APK_KEY_PASSWORD APK_KEY_ALIAS ANDROID_HOME; do
  if [[ -z "${!name:-}" ]]; then
    printf 'Missing required environment variable: %s
' "$name" >&2
    exit 1
  fi
done
unsigned='android/app/build/outputs/apk/release/app-release-unsigned.apk'
if [[ ! -f "$unsigned" ]]; then
  printf 'Android release APK not found: %s
' "$unsigned" >&2
  exit 1
fi
mkdir -p dist
build_tools="$ANDROID_HOME/build-tools/35.0.0"
aligned="$(mktemp "${TMPDIR:-/tmp}/pme-aligned.XXXXXX.apk")"
keystore="$(mktemp "${TMPDIR:-/tmp}/pme-key.XXXXXX.jks")"
trap 'rm -f "$aligned" "$keystore"' EXIT
chmod 600 "$keystore"
printf '%s' "$APK_KEYSTORE_B64" | base64 --decode > "$keystore"
"$build_tools/zipalign" -p -f 4 "$unsigned" "$aligned"
output="dist/Falcon-Police-Mode-android-${version}.apk"
"$build_tools/apksigner" sign \
  --ks "$keystore" \
  --ks-key-alias "$APK_KEY_ALIAS" \
  --ks-pass env:APK_KEYSTORE_PASSWORD \
  --key-pass env:APK_KEY_PASSWORD \
  --out "$output" "$aligned"
"$build_tools/apksigner" verify --verbose "$output"
printf '%s
' "$output"
