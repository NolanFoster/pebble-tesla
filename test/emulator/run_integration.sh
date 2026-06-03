#!/usr/bin/env bash
# Best-effort end-to-end smoke test in the Pebble QEMU emulator.
#
# This is an INTEGRATION test, not a unit test: it builds the real .pbw, boots
# it in the basalt emulator (pypkjs runs the JS side), and checks the app comes
# up cleanly. It is skipped — exit 0 — when the Pebble toolchain is absent, so
# it is safe to invoke anywhere (and is deliberately excluded from CI).
#
# Prereqs (local only): Rebble `pebble-tool` and `qemu-system-arm`.
# See test/emulator/README.md.

set -euo pipefail
cd "$(dirname "$0")/../.."

if ! command -v pebble >/dev/null 2>&1; then
  echo "pebble CLI not found — skipping emulator integration test."
  exit 0
fi

LOG="$(mktemp)"
trap 'pebble kill >/dev/null 2>&1 || true; rm -f "$LOG"' EXIT

echo "==> pebble build"
pebble build

echo "==> install on basalt emulator"
pebble install --emulator basalt --logs > "$LOG" 2>&1 &
LOGS_PID=$!
sleep 8   # give pypkjs time to evaluate index.js and fire 'ready'

# Drive the menu: open Controls and trigger Refresh.
pebble emu-button select >/dev/null 2>&1 || true

sleep 3
kill "$LOGS_PID" >/dev/null 2>&1 || true

echo "==> assertions"
# index.js logs this on the JS 'ready' event (src/pkjs/index.js).
if ! grep -q "Tesla Control JS ready" "$LOG"; then
  echo "FAIL: JS 'ready' log line not seen"; cat "$LOG"; exit 1
fi
# Unconfigured run must surface the guard error rather than crash (network-free,
# deterministic). With a token/VIN set this would instead show live state.
if grep -qiE "error|crash|abort|segm" "$LOG" \
   && ! grep -q "Set token & VIN" "$LOG"; then
  echo "FAIL: unexpected error in logs"; cat "$LOG"; exit 1
fi

echo "PASS: app booted and JS bridge initialised."
