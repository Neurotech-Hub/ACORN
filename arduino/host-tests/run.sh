#!/usr/bin/env bash
# Builds and runs the AcornCore host unit tests (no hardware needed).
# Usage: ./run.sh [--write-golden]
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
LIBS="$HERE/../libraries"
BUILD="$HERE/build"
CXX="${CXX:-c++}"

mkdir -p "$BUILD" "$HERE/golden"
"$CXX" -std=c++17 -O1 -Wall -Wextra -Werror=return-type \
  -I"$LIBS/AcornCore/src" -I"$LIBS/ArduinoJson/src" \
  "$HERE"/test_*.cpp "$LIBS"/AcornCore/src/*.cpp \
  -o "$BUILD/acorn-host-tests"

"$BUILD/acorn-host-tests" --golden "$HERE/golden" "$@"
