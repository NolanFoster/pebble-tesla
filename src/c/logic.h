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
} VehicleState;

// Does this command carry a TEMP_DELTA argument in the outbox dict?
bool cmd_has_temp_delta(int cmd);

// Status-row subtitles. Each writes a NUL-terminated string into `out` (size n).
void fmt_battery_subtitle(const VehicleState *s, char *out, size_t n);
void fmt_lock_subtitle(const VehicleState *s, char *out, size_t n);
void fmt_climate_subtitle(const VehicleState *s, char *out, size_t n);
void fmt_power_subtitle(const VehicleState *s, char *out, size_t n);

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
