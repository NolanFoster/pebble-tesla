#!/usr/bin/env bash
# Best-effort end-to-end smoke test in the Pebble QEMU emulator.
#
# This is an INTEGRATION test, not a unit test: it builds the real .pbw, boots
# it in the basalt emulator (pypkjs runs the JS side), installs the app, and
# checks the JS bridge comes up. It is skipped — exit 0 — when the Pebble
# toolchain is absent, so it is safe to invoke anywhere (and is deliberately
# excluded from CI). See test/emulator/README.md.
#
# Prereqs (local only):
#   - Rebble / Core Devices `pebble-tool` (pip install pebble-tool) + an SDK
#     (`pebble sdk install latest`), which bundles the custom `qemu-pebble`.
#   - SDL2 + audio libs for qemu-pebble: libsdl2-2.0-0 libpulse0 libsndio7.0
#   - A display. The emulator opens an SDL window; on a headless host this
#     script auto-wraps itself in `xvfb-run` (install the `xvfb` package).
#   - Heads-up: on hosts without IPv6, pypkjs may fail to bind its debug server
#     (it listens on "" which resolves to IPv6). See the README workaround.

set -euo pipefail
cd "$(dirname "$0")/../.."

if ! command -v pebble >/dev/null 2>&1; then
  echo "pebble CLI not found — skipping emulator integration test."
  exit 0
fi

# The emulator needs an X display for its SDL window. If none is present,
# re-exec under a virtual framebuffer so this works in containers/CI too.
if [ -z "${DISPLAY:-}" ] && [ -z "${PEBBLE_XVFB:-}" ]; then
  if command -v xvfb-run >/dev/null 2>&1; then
    echo "No DISPLAY — re-running under xvfb-run."
    exec env PEBBLE_XVFB=1 xvfb-run -a "$0" "$@"
  fi
  echo "No DISPLAY and xvfb-run not found — skipping emulator integration test."
  exit 0
fi

LOG="$(mktemp)"
trap 'pebble kill >/dev/null 2>&1 || true; rm -f "$LOG"' EXIT

echo "==> pebble build"
pebble build

echo "==> install on basalt emulator (streams logs)"
pebble install --emulator basalt --logs > "$LOG" 2>&1 &
LOGS_PID=$!

# Wait (up to ~60s) for the JS 'ready' line rather than guessing with a sleep —
# first firmware boot can be slow.
echo "==> waiting for app to boot"
for _ in $(seq 1 60); do
  grep -q "Tesla Control JS ready" "$LOG" && break
  sleep 1
done

# Drive the menu: tapping a Status row sends CMD_REFRESH to the phone.
pebble emu-button click select >/dev/null 2>&1 || true
sleep 2

kill "$LOGS_PID" >/dev/null 2>&1 || true

echo "==> assertions"
# index.js logs this on the JS 'ready' event (src/pkjs/index.js).
if ! grep -q "Tesla Control JS ready" "$LOG"; then
  echo "FAIL: JS 'ready' log line not seen"; cat "$LOG"; exit 1
fi
# A real crash (firmware fault / pypkjs traceback) must fail the run. The
# "QemuInboundPacket.footer" warning from pypkjs is benign and ignored; the
# unconfigured "Set token & VIN" path is the expected, network-free behaviour.
if grep -qiE "traceback|segfault|fatal|core dumped" "$LOG"; then
  echo "FAIL: crash signature in logs"; cat "$LOG"; exit 1
fi

echo "PASS: emulator booted, app installed, JS bridge initialised."
