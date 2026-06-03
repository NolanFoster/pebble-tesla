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

// Snapshot of vehicle state used purely for rendering decisions.
typedef struct {
  int  battery;       // percent, or <0 when unknown
  int  range;         // miles
  int  inside_temp;   // displayed units (already converted by the phone)
  int  target_temp;   // displayed units
  bool locked;
  bool climate_on;
  bool online;
} VehicleState;

// Does this command carry a TEMP_DELTA argument in the outbox dict?
bool cmd_has_temp_delta(int cmd);

// Status-row subtitles. Each writes a NUL-terminated string into `out` (size n).
void fmt_battery_subtitle(const VehicleState *s, char *out, size_t n);
void fmt_lock_subtitle(const VehicleState *s, char *out, size_t n);
void fmt_climate_subtitle(const VehicleState *s, char *out, size_t n);

// Action-row dynamic labels (returns a static string; never NULL).
const char *lock_toggle_label(const VehicleState *s);     // "Lock" / "Unlock"
const char *climate_toggle_label(const VehicleState *s);  // "Climate On" / "Climate Off"

// Which command a toggle row should send given current state.
int lock_toggle_cmd(const VehicleState *s);     // CMD_LOCK / CMD_UNLOCK
int climate_toggle_cmd(const VehicleState *s);  // CMD_CLIMATE_ON / CMD_CLIMATE_OFF

#endif // LOGIC_H
