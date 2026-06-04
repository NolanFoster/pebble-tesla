# Tesla Control — App Store Listing

Copy for the public beta listing (Rebble / Pebble appstore). Lengths are kept
within typical store limits; trim per the field you're filling.

---

## Name

**Tesla Control**

## Tagline (one line)

Your Tesla, on your wrist.

## Short description (1–2 sentences)

Lock or unlock, start climate, pop the frunk or trunk, open the charge port, and
check battery, range, and status — straight from your Pebble. Commands route
securely through Tessie, so all you need is your token and VIN.

## Long description

Tesla Control puts the things you actually reach for your phone to do right on
your wrist.

The main screen is a status card showing your vehicle's name, battery and range,
lock state, climate, and whether the car is awake, asleep, or settling to sleep.
A three-button action bar gives you instant lock/unlock and climate on/off — each
button's icon reflects the current state — while the Select button opens **More
Controls** for temperature ±1°, **Open Frunk**, **Open Trunk**, **Charge Port**,
and Refresh. Every command shows a quick confirmation overlay.

Commands route through **Tessie** (api.tessie.com), which handles Tesla's OAuth
and Vehicle Command Protocol signing for you — so the watchapp only needs your
Tessie API token and VIN, entered once on the phone. If the car is asleep, Tesla
Control wakes it automatically before sending your command.

```
Watch (button menu)  ⇄  PebbleKit JS (phone)  ⇄  Tessie API  ⇄  your Tesla
```

**Supported watches:** Pebble Time / Time Steel (basalt), Pebble Time Round
(chalk), and Pebble Time 2 (diorite / emery). On color watches the action bar
uses a Tesla-red accent; on black-and-white it falls back cleanly to monochrome.

## Feature bullets

- 🔒 Lock / unlock with a single button — icon shows the current state
- ❄️ Climate on / off, plus Temp +1° / −1° (15–28 °C, optional °F display)
- 🚗 Open Frunk, Open Trunk, and Charge Port from the More Controls menu
- 🔋 Battery %, range, lock, climate, and awake/asleep status at a glance
- 😴 Wakes a sleeping car automatically before sending a command
- 🎨 Color and black-and-white layouts; round and rectangular displays

## Setup (beta)

1. A **Tessie** account with your vehicle linked and an active subscription.
2. A Tessie **API token** — https://dash.tessie.com/settings/api
3. Your vehicle **VIN**.
4. Install the app, then open the Pebble phone app → **Tesla Control** →
   **Settings**, and enter your token, VIN, and (optionally) °F display.

Your token is stored only in the phone app's local storage, sent directly to
api.tessie.com over HTTPS, and masked after saving.

## Beta notes & known limitations

- **Public beta** — feedback welcome. Expect rough edges.
- Requires a Tessie account/subscription; this app does not talk to Tesla
  directly.
- Commands only work when the car is reachable; waking an asleep car can take a
  while, and slow reads time out at ~35s ("Timed out (car asleep?)") — just
  retry.
- **Privacy:** anyone with access to your unlocked phone can open the settings
  page. The saved token is masked, not editable-in-the-clear. Consider a
  Tessie token scoped to only the commands you need, if available.

## Category / tags

Utilities · Tools · Tesla · Car · EV

## Screenshots

See `store/screenshots/<platform>/` — one set per platform (basalt, chalk,
diorite, emery), captured in the Pebble emulator: `01_status.png` (the status
card) and `02_controls.png` (the More Controls menu, showing the frunk/trunk
car icons).
