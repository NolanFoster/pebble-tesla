// Pure presentation/dispatch logic for the Tesla Control watchapp.
//
// This header deliberately does NOT include <pebble.h> so the logic can be
// compiled and unit-tested on the host (see test/c/). main.c includes this
// header and feeds it a VehicleState built from its cached `s_*` globals.

#ifndef LOGIC_H
#define LOGIC_H

#include <stdbool.h>
#include <stddef.h>

// ---- Command codes sent watch -> phone (must match CMD in src/pkjs/index.js
//      and the messageKeys order is unrelated; these are CMD *values*). ----
typedef enum {
  CMD_REFRESH      = 0,
  CMD_LOCK         = 1,
  CMD_UNLOCK       = 2,
  CMD_CLIMATE_ON   = 3,
  CMD_CLIMATE_OFF  = 4,
  CMD_TEMP_UP      = 5,
  CMD_TEMP_DOWN    = 6,
  CMD_FRUNK        = 7,
  CMD_TRUNK        = 8,
  CMD_CHARGE_PORT  = 9,
  CMD_WAKE         = 10,
  CMD_CHARGE_START = 11,
  CMD_CHARGE_STOP  = 12,
  CMD_LIMIT_UP     = 13,  // raise charge limit (+5%)
  CMD_LIMIT_DOWN   = 14,  // lower charge limit (-5%)
} TeslaCommand;

// Vehicle power status from Tessie GET /{vin}/status. Commands can only be sent
// when AWAKE; otherwise the phone issues a wake first. Sent watch<->phone as the
// AWAKE message key (int). Keep these values in sync with awakeCode() in index.js.
typedef enum {
  AWAKE_UNKNOWN = -1,
  AWAKE_ASLEEP  = 0,
  AWAKE_AWAKE   = 1,
  AWAKE_WAITING = 2,  // waiting_for_sleep
} AwakeStatus;

// Exterior paint color, used to theme the app's accent (action bar + menu
// highlight) so the UI matches the car. Sent phone->watch as the PAINT_COLOR
// message key (int). The phone maps Tessie's vehicle_config.exterior_color /
// option_codes onto these values (see paintCode() in index.js); the watch maps
// each value onto an accent GColor plus a contrast-safe foreground (see
// theme_for_paint() in main.c). Keep the two in sync.
typedef enum {
  PAINT_UNKNOWN = -1,  // not reported -> watch keeps the default brand-red accent
  PAINT_RED     = 0,   // Red Multi-Coat / Ultra Red / Midnight Cherry Red
  PAINT_WHITE   = 1,   // Pearl White Multi-Coat
  PAINT_BLACK   = 2,   // Solid Black / Obsidian Black
  PAINT_SILVER  = 3,   // Silver Metallic / Quicksilver / Titanium (light)
  PAINT_GREY    = 4,   // Midnight Silver / Stealth Grey (dark)
  PAINT_BLUE    = 5,   // Deep Blue Metallic
} PaintColor;

// Charging status, derived from Tessie charge_state.charging_state. Sent
// phone->watch as the CHARGING message key (int). Keep in sync with chargeCode()
// in index.js. "Connected" (plugged in) is CHARGING/STOPPED/COMPLETE; only those
// states offer the Start/Stop-charging control.
typedef enum {
  CHARGE_UNKNOWN      = -1,  // not reported
  CHARGE_DISCONNECTED = 0,   // unplugged
  CHARGE_STOPPED      = 1,   // plugged in, not charging (or NoPower)
  CHARGE_CHARGING     = 2,   // actively charging (or Starting)
  CHARGE_COMPLETE     = 3,   // plugged in, reached the limit
} ChargeState;

// Snapshot of vehicle state used purely for rendering decisions.
typedef struct {
  int  battery;       // percent, or <0 when unknown
  int  range;         // miles
  int  inside_temp;   // displayed units (already converted by the phone)
  int  target_temp;   // displayed units
  bool locked;
  bool climate_on;
  bool online;
  int  awake;         // AwakeStatus
  int  charging;      // ChargeState
  int  charge_limit;  // target charge percent, or <0 when unknown
  int  charge_eta;    // minutes to the limit while charging, or <0 when unknown
} VehicleState;

// Does this command carry a TEMP_DELTA argument in the outbox dict?
bool cmd_has_temp_delta(int cmd);

// "More Controls" menu rows. The Wake row is only present when the vehicle is
// asleep, so the visible row set is computed at render time from the state.
typedef enum {
  CTRL_ROW_WAKE = 0,
  CTRL_ROW_TEMP_UP,
  CTRL_ROW_TEMP_DOWN,
  CTRL_ROW_FRUNK,
  CTRL_ROW_TRUNK,
  CTRL_ROW_CHARGE_PORT,
  CTRL_ROW_CHARGE,       // start/stop charging (only when plugged in)
  CTRL_ROW_LIMIT_UP,     // charge limit +5%
  CTRL_ROW_LIMIT_DOWN,   // charge limit -5%
  CTRL_ROW_REFRESH,
} ControlsRow;
#define CONTROLS_MAX_ROWS 10

// Fills `rows` with the visible rows for the current state and returns the
// count (<= max). CTRL_ROW_WAKE is included first only when the car is asleep;
// CTRL_ROW_CHARGE only when the car is plugged in (see charge_is_connected).
int         controls_menu_rows(const VehicleState *s, ControlsRow *rows, int max);
const char *controls_row_label(ControlsRow row);     // "Wake", "Temp +1°", …
int         controls_row_cmd(ControlsRow row);        // CMD_WAKE, CMD_TEMP_UP, …
int         controls_row_temp_delta(ControlsRow row); // +1 / -1 / 0

// Charging helpers. The Start/Stop-charging row is a single state-aware toggle
// (like the action-bar lock/climate toggles), so its label and command depend on
// whether the car is currently charging.
bool        charge_is_connected(const VehicleState *s);  // plugged in?
const char *charge_toggle_label(const VehicleState *s);  // "Stop Charging" / "Start Charging"
int         charge_toggle_cmd(const VehicleState *s);    // CMD_CHARGE_STOP / CMD_CHARGE_START

// Status-row subtitles. Each writes a NUL-terminated string into `out` (size n).
void fmt_battery_subtitle(const VehicleState *s, char *out, size_t n);
void fmt_lock_subtitle(const VehicleState *s, char *out, size_t n);
void fmt_climate_subtitle(const VehicleState *s, char *out, size_t n);
void fmt_power_subtitle(const VehicleState *s, char *out, size_t n);

// Charging footer, e.g. "Charging · 1h20m", "Charging" (eta unknown) or
// "Charge complete". Writes "" when not charging/complete. `charge_show_status`
// says whether this should replace the power footer (true while charging or once
// complete; otherwise the awake/power state is the more useful footer).
void fmt_charge_subtitle(const VehicleState *s, char *out, size_t n);
bool charge_show_status(const VehicleState *s);

// True when a transient STATUS string is a *final* confirmation (drives a short
// success vibration) rather than an in-progress message. Progress messages end
// with an ellipsis ("Waking…", "Locking…"); confirmations ("Locked") do not.
bool status_is_confirmation(const char *msg);

// Battery gauge text, split so the percentage can sit inside a circular gauge
// and the range just beneath it. `out` is always NUL-terminated.
void fmt_battery_pct(const VehicleState *s, char *out, size_t n);  // "84%" or "—"
void fmt_range(const VehicleState *s, char *out, size_t n);        // "240 mi" or ""

// Battery percentage as bare digits, for the LECO numbers-only hero font (which
// has no '%' glyph — the percent sign is drawn separately). "84" or "—".
void fmt_battery_num(const VehicleState *s, char *out, size_t n);

// Charge level bucket, used to color the battery gauge arc. Returned as an enum
// (not a GColor) so this stays host-testable without <pebble.h>.
typedef enum {
  BATTERY_UNKNOWN = -1,  // battery < 0
  BATTERY_LOW     = 0,   // <= 20%
  BATTERY_MED,           // <= 50%
  BATTERY_HIGH,          // > 50%
} BatteryLevel;
BatteryLevel battery_level(int pct);

// Human label for an AwakeStatus value ("Awake"/"Asleep"/"Waiting for sleep"/"—").
const char *awake_label(int awake);

// Header text for the Status section: the vehicle's name if set, else "Status".
// `name` may be NULL or empty.
const char *status_header_text(const char *name);

// Action-row dynamic labels (returns a static string; never NULL).
const char *lock_toggle_label(const VehicleState *s);     // "Lock" / "Unlock"
const char *climate_toggle_label(const VehicleState *s);  // "Climate On" / "Climate Off"

// Which command a toggle row should send given current state.
int lock_toggle_cmd(const VehicleState *s);     // CMD_LOCK / CMD_UNLOCK
int climate_toggle_cmd(const VehicleState *s);  // CMD_CLIMATE_ON / CMD_CLIMATE_OFF

// Which action-bar glyph to show for the lock / climate buttons. The icon
// reflects the *current* vehicle state (a closed padlock when locked, a running
// fan when climate is on) so it doubles as a status indicator. Returns an enum
// rather than a RESOURCE_ID_* so this stays host-testable (no <pebble.h>).
typedef enum {
  ICON_KIND_LOCKED = 0,    // doors locked
  ICON_KIND_UNLOCKED,      // doors unlocked
  ICON_KIND_CLIMATE_ON,    // climate running
  ICON_KIND_CLIMATE_OFF,   // climate off
} IconKind;

IconKind lock_toggle_icon(const VehicleState *s);     // ICON_KIND_LOCKED / _UNLOCKED
IconKind climate_toggle_icon(const VehicleState *s);  // ICON_KIND_CLIMATE_ON / _OFF

#endif // LOGIC_H
