# Emulator integration test

`run_integration.sh` is a **best-effort end-to-end smoke test** that builds the
real watchapp and boots it in the Pebble QEMU emulator. Unlike the JS (Jest) and
C (Unity) unit tests, it exercises the whole stack: the compiled C app, the
AppMessage bridge, and `index.js` running under `pypkjs`.

## Why it's separate from the unit tests / CI

The Pebble emulator is an **integration tool, not a unit-test harness** — there
is no built-in assertion framework you run *against* it. You boot the app and
observe it. It also needs a full toolchain that the CI container doesn't have,
so this script is excluded from `.github/workflows/ci.yml`. The script
self-skips (`exit 0`) when `pebble` isn't on `PATH`, so it's harmless to run
anywhere.

## Prerequisites (local)

- Rebble [`pebble-tool`](https://github.com/pebble-dev/pebble-tool) (the
  community-maintained CLI; the original Pebble SDK was discontinued in 2016 and
  is now kept alive by Rebble / Core Devices).
- `qemu-system-arm`.
- A working Pebble SDK install (`pebble sdk install latest`).

## Run

```bash
bash test/emulator/run_integration.sh
```

What it does:

1. `pebble build` → produces the `.pbw`.
2. `pebble install --emulator basalt --logs` → boots basalt, streams logs.
3. Presses **select** to enter Controls / trigger Refresh.
4. Asserts the JS `ready` line (`"Tesla Control JS ready"`) appears and the app
   didn't crash.

### Deterministic, network-free assertion

With no Tessie token/VIN configured, `tessie()` short-circuits and the app shows
`Set token & VIN in app settings` — a stable signal that the watch↔phone bridge
works without hitting the network. To exercise live data, configure a token/VIN
via the app's settings page (or pre-seed `localStorage` in `pypkjs`) before
running.
