#!/usr/bin/env bash
# Run the AcornCore host unit tests, regenerate the embedded web UI header,
# then compile the ACORN firmware and every isolated hardware test with
# arduino-cli.
#
#   ./arduino/compile-tests.sh              # host tests + compile everything
#   ./arduino/compile-tests.sh test_hardware  # host tests + matching sketches only
#   ACORN_SKIP_HOST=1 ./arduino/compile-tests.sh   # compile only
#
# Requires: arduino-cli with the esp32:esp32 core (3.x), a C++17 host
# compiler (c++/clang++), and Node 18+ for the mock golden comparison.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FQBN="${ACORN_FQBN:-esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=enabled}"
FILTER="${1:-}"
OTA_SLOT_BYTES=6553600  # 0x640000, see acorn/partitions.csv

if [[ -z "${ACORN_SKIP_HOST:-}" ]]; then
  echo "---- host tests (AcornCore) ----"
  if ! out="$("$HERE/host-tests/run.sh" 2>&1)"; then
    printf '%s\n' "$out"
    exit 1
  fi
  printf '%s\n' "$out" | tail -n 1
  if command -v node >/dev/null 2>&1; then
    if ! out="$(node "$HERE/../tools/check-mock-golden.mjs" 2>&1)"; then
      printf '%s\n' "$out"
      exit 1
    fi
    printf '%s\n' "$out" | tail -n 1
  else
    echo "node not found: skipping mock golden comparison"
  fi
  echo "---- arduino-cli ----"
fi

# Keep the embedded web UI in sync with web/ (the header is committed so the
# sketch also builds without Node, e.g. from the Arduino IDE).
if command -v node >/dev/null 2>&1; then
  if ! out="$(node "$HERE/../tools/build-web.mjs" --header "$HERE/acorn/src/web/web_assets.h" 2>&1)"; then
    printf '%s\n' "$out"
    exit 1
  fi
else
  echo "node not found: using the committed acorn/src/web/web_assets.h"
fi

if ! cmp -s "$HERE/acorn/partitions.csv" "$HERE/tests/test_ota/partitions.csv"; then
  echo "error: tests/test_ota/partitions.csv differs from acorn/partitions.csv" >&2
  exit 1
fi

sketches=("$HERE/acorn")
for dir in "$HERE"/tests/*/; do sketches+=("${dir%/}"); done

pass=0
fail=0
failed=()
for sketch in "${sketches[@]}"; do
  name="$(basename "$sketch")"
  [[ -n "$FILTER" && "$name" != *"$FILTER"* ]] && continue
  printf '%-18s ' "$name"
  extra=()
  # The board's size check assumes the stock scheme; use the real OTA slot size.
  [[ -f "$sketch/partitions.csv" ]] && extra=(--build-property "upload.maximum_size=$OTA_SLOT_BYTES")
  if out="$(arduino-cli compile --fqbn "$FQBN" --libraries "$HERE/libraries" \
      --warnings default ${extra[@]+"${extra[@]}"} "$sketch" 2>&1)"; then
    size="$(printf '%s\n' "$out" | grep -Eo 'Sketch uses [0-9]+ bytes \([0-9]+%\)' || true)"
    echo "ok   ${size#Sketch uses }"
    pass=$((pass + 1))
  else
    echo "FAIL"
    printf '%s\n' "$out" | grep -E 'error|warning' | head -20 || true
    fail=$((fail + 1))
    failed+=("$name")
  fi
done

echo "---- $pass compiled, $fail failed ----"
if ((fail)); then
  printf 'failed: %s\n' "${failed[*]}"
  exit 1
fi
