#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if [[ -z "${ANDROID_HOME:-}" && -z "${ANDROID_SDK_ROOT:-}" ]]; then
  echo 'Set ANDROID_HOME to your Android SDK directory (for example $HOME/Android/Sdk).' >&2
  exit 1
fi
if [[ ! -x .tools/gradle-8.13/bin/gradle ]]; then
  mkdir -p .tools
  curl -fL --retry 2 https://services.gradle.org/distributions/gradle-8.13-bin.zip -o .tools/gradle.zip
  unzip -qo .tools/gradle.zip -d .tools
  rm .tools/gradle.zip
fi
.tools/gradle-8.13/bin/gradle --no-daemon assembleDebug
printf '\nAPK: %s\n' "$PWD/app/build/outputs/apk/debug/app-debug.apk"
