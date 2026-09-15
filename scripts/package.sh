#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

version="${VERSION:-$(tr -d '[:space:]' < VERSION)}"
os="$(uname -s | tr '[:upper:]' '[:lower:]')"
arch="$(uname -m)"
name="remoteos-sdl-${version}-${os}-${arch}"
stage="build/package/$name"

rm -rf build/package
mkdir -p "$stage/bin"
cp -f remoteos-sdl "$stage/bin/remoteos-sdl"
cp -f LICENSE README.md PROTOCOL.md CHANGELOG.md "$stage/"
mkdir -p dist
tar -C build/package -czf "dist/$name.tar.gz" "$name"
if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "dist/$name.tar.gz" > "dist/$name.tar.gz.sha256"
else
    sha256sum "dist/$name.tar.gz" > "dist/$name.tar.gz.sha256"
fi
printf '%s\n' "dist/$name.tar.gz"
