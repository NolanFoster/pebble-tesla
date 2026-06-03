# Emulator integration test

`run_integration.sh` is a **best-effort end-to-end smoke test** that builds the
real watchapp and boots it in the Pebble QEMU emulator. Unlike the JS (Jest) and
C (Unity) unit tests, it exercises the whole stack: the compiled C app, the
AppMessage bridge, and `index.js` running under `pypkjs`.

It has been run successfully end-to-end — the app installs, the menu renders
(driven by `src/c/logic.c`), and PebbleKit JS logs `Tesla Control JS ready`.

## Why it's separate from the unit tests / CI

The Pebble emulator is an **integration tool, not a unit-test harness** — there
is no built-in assertion framework you run *against* it. You boot the app and
observe it (logs / screenshots). It also needs a full toolchain and a GUI/SDL
stack that CI runners don't have, so this script is excluded from
`.github/workflows/ci.yml`. The script self-skips (`exit 0`) when `pebble` isn't
on `PATH` (or when there's no display and `xvfb-run` is unavailable), so it's
harmless to run anywhere.

## Prerequisites

The original Pebble SDK was discontinued in 2016; the tooling is now maintained
by Rebble / Core Devices.

```bash
# 1. CLI + JS runtime (pulls in pypkjs + stpyv8)
pip install pebble-tool

# 2. SDK (bundles the custom qemu-pebble + firmware images)
pebble sdk install latest

# 3. Native libs qemu-pebble links against (Debian/Ubuntu)
sudo apt-get install -y libsdl2-2.0-0 libpulse0 libsndio7.0

# 4. Headless only: virtual X server (the emulator opens an SDL window)
sudo apt-get install -y xvfb
```

## Run

```bash
bash test/emulator/run_integration.sh
```

What it does:

1. `pebble build` → produces the `.pbw`.
2. If there's no `DISPLAY`, re-execs itself under `xvfb-run`.
3. `pebble install --emulator basalt --logs` → boots basalt, streams logs.
4. Waits (up to ~60 s) for `Tesla Control JS ready`.
5. `pebble emu-button click select` → taps a Status row (sends `CMD_REFRESH`).
6. Asserts the JS ready line appeared and there's no crash signature in the logs.

To capture a screenshot of the running app:

```bash
DISPLAY=:0 pebble screenshot home.png   # or under the same xvfb display
```

### Deterministic, network-free assertion

With no Tessie token/VIN configured, `tessie()` short-circuits and the watch
shows `Set token & VIN in app settings` — a stable signal that the watch↔phone
bridge works without hitting the network. To exercise live data, configure a
token/VIN via the app's settings page before running.

## Known gotchas (hit while validating this)

- **`qemu-pebble: error while loading shared libraries: libSDL2-2.0.so.0`** —
  install the native libs in step 3 above.
- **No IPv6 host (some containers): `OSError: [Errno 97] Address family not
  supported by protocol` then `[Errno 111] Connection refused`.** `pypkjs`
  binds its debug websocket server to `""`, which gevent resolves to IPv6 — and
  fails where IPv6 is compiled out. Workaround until upstream is fixed: bind it
  to IPv4 in your installed copy of
  `pypkjs/runner/websocket.py` —
  change `pywsgi.WSGIServer(("", self.port), ...)` to
  `pywsgi.WSGIServer(("127.0.0.1", self.port), ...)`.
- **`[PHONESIM] [WARNING] Exception decoding QemuInboundPacket.footer`** — benign
  pypkjs/QEMU framing warning; ignored by the test's crash check.
