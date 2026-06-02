# Tesla Control — Pebble Time 2 watchapp

Control your Tesla from your wrist: lock/unlock, climate on/off + temperature,
open frunk/trunk/charge port, and see battery, range, and lock/climate status.

Commands route through **Tessie** (api.tessie.com), which handles Tesla's OAuth
and Vehicle Command Protocol signing for you — so the watchapp just needs your
Tessie token and VIN.

```
Watch (C, button menu)  ⇄  PebbleKit JS (phone)  ⇄  Tessie API  ⇄  your Tesla
```

## Prerequisites

1. A **Tessie** account with your vehicle linked, plus an active subscription.
2. A Tessie **API token** → https://dash.tessie.com/settings/api
3. Your vehicle **VIN**.
4. Local Pebble SDK (you have this). The modern open-source toolchain is the
   Rebble fork:

   ```bash
   pip install pebble-tool          # or use your existing install
   # Newer SDK distribution:
   # https://github.com/google/pebble  (PebbleOS, open-sourced Jan 2025)
   ```

## Build

From the project root:

```bash
pebble build
```

This compiles for basalt, chalk, diorite, and emery. The Pebble Time 2 is the
new Core Devices hardware running open-source PebbleOS; it reports as a
rectangular color platform, so the **basalt/emery** assets apply. If you hit a
platform-name mismatch once PT2 ships with its own platform id, add it to
`targetPlatforms` in `package.json` and rebuild.

## Install

With the watch paired to the Pebble phone app and developer connection enabled:

```bash
pebble install --phone <YOUR_PHONE_IP>
# or, with the emulator:
pebble install --emulator basalt --logs
```

## Configure

Open the Pebble phone app → **Tesla Control** → **Settings**. Enter:

- Tessie API token
- VIN
- (optional) show temperatures in °F

The token is stored only in the phone app's local storage and sent directly to
api.tessie.com over HTTPS. It is never echoed back to the settings page once
saved.

## Using it

- The main screen is a button-driven menu (PT2's touchscreen works too, but
  buttons are the reliable primary input).
- **Status** section: Battery / Doors / Climate. Select any row to refresh.
- **Controls** section: Lock/Unlock (label flips with state), Climate On/Off,
  Temp ±1°, Open Frunk, Open Trunk, Charge Port, Refresh.
- A short overlay shows "Locking…", then the result, then auto-dismisses.

## Notes & gotchas

- **Sleeping car:** the first command after the car sleeps can take 10–30s while
  it wakes. The JS timeout is 35s; you'll see "Timed out (car asleep?)" if it
  exceeds that — just retry.
- **Temperature** is tracked in °C internally (Tessie's `set_temperature` takes
  Celsius) and clamped to 15–28°C. The ±1° buttons step from the last known
  target; a refresh re-syncs it to the car's actual setting.
- **Endpoints used:** `GET /{vin}/state`, `POST /{vin}/command/{lock|unlock|
  start_climate|stop_climate|set_temperature|activate_front_trunk|
  activate_rear_trunk|open_charge_port}`.
- **Security:** anyone with physical access to your unlocked phone could open
  the settings page, but the saved token is masked. Consider a Tessie token
  scoped to only the commands you need if Tessie offers scoping.

## Files

- `src/c/main.c` — watch UI, menu, AppMessage, command dispatch
- `src/pkjs/index.js` — Tessie bridge + inline settings page
- `package.json` — app manifest + message keys
- `wscript` — build script
