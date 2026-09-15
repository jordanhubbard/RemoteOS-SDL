#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

version="$(tr -d '[:space:]' < VERSION)"
grep -Fq "# RemoteOS-SDL v$version" RELEASE-NOTES.md
make clean
make test
make package
test -s "$(find dist -name "remoteos-sdl-$version-*.tar.gz" -print -quit)"
