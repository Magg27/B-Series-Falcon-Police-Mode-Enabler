#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
version="${1:-dev}"
case "$version" in
  *[!a-zA-Z0-9._-]*|'') echo 'Invalid release version.' >&2; exit 2 ;;
esac
make all test
mkdir -p dist
archive="Falcon-Police-Mode-linux-x86_64-${version}.zip"
rm -f "dist/$archive"
zip -q -j "dist/$archive" bin/pme-cli bin/pme-gui README.md LICENSE
printf '%s
' "dist/$archive"
