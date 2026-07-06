#!/usr/bin/env bash
# Build the Linux reference host (C++ + GTK3 + WebKitGTK) via CMake + Ninja. Shared by `make
# host-linux` and, in spirit, the per-app CLI build. Needs: g++, cmake, ninja, and the -dev packages
# for gtk+-3.0, webkit2gtk-4.1, libsoup-3.0, libsodium.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD="${1:-$DIR/build}"
cmake -S "$DIR" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD"
echo "built $BUILD/EngawaHost"
