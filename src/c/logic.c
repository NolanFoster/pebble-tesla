// Pure logic for the Tesla Control watchapp — no Pebble SDK dependency.
// Format strings here must stay byte-identical to the originals in main.c.

#include "logic.h"
#include <stdio.h>

bool cmd_has_temp_delta(int cmd) {
  return cmd == CMD_TEMP_UP || cmd == CMD_TEMP_DOWN;
}

void fmt_battery_subtitle(const VehicleState *s, char *out, size_t n) {
  if (s->battery >= 0)
    snprintf(out, n, "%d%%  •  %d mi", s->battery, s->range);
  else
    snprintf(out, n, "—");
}

void fmt_lock_subtitle(const VehicleState *s, char *out, size_t n) {
  snprintf(out, n, "%s", s->locked ? "Locked" : "Unlocked");
}

void fmt_climate_subtitle(const VehicleState *s, char *out, size_t n) {
  if (s->climate_on)
    snprintf(out, n, "On  •  in %d° → %d°", s->inside_temp, s->target_temp);
  else
    snprintf(out, n, "Off  •  in %d°", s->inside_temp);
}

const char *awake_label(int awake) {
  switch (awake) {
    case AWAKE_AWAKE:   return "Awake";
    case AWAKE_ASLEEP:  return "Asleep";
    case AWAKE_WAITING: return "Waiting for sleep";
    default:            return "—";
  }
}

void fmt_power_subtitle(const VehicleState *s, char *out, size_t n) {
  snprintf(out, n, "%s", awake_label(s->awake));
}

const char *lock_toggle_label(const VehicleState *s) {
  return s->locked ? "Unlock" : "Lock";
}

const char *climate_toggle_label(const VehicleState *s) {
  return s->climate_on ? "Climate Off" : "Climate On";
}

int lock_toggle_cmd(const VehicleState *s) {
  return s->locked ? CMD_UNLOCK : CMD_LOCK;
}

int climate_toggle_cmd(const VehicleState *s) {
  return s->climate_on ? CMD_CLIMATE_OFF : CMD_CLIMATE_ON;
}
