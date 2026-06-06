// Pure logic for the Tesla Control watchapp — no Pebble SDK dependency.
// Format strings here must stay byte-identical to the originals in main.c.

#include "logic.h"
#include <stdio.h>
#include <string.h>

bool cmd_has_temp_delta(int cmd) {
  return cmd == CMD_TEMP_UP || cmd == CMD_TEMP_DOWN;
}

int controls_menu_rows(const VehicleState *s, ControlsRow *rows, int max) {
  int n = 0;
  // Wake only makes sense (and is only offered) when the car is asleep.
  if (s->awake == AWAKE_ASLEEP && n < max) rows[n++] = CTRL_ROW_WAKE;
  static const ControlsRow head[] = {
    CTRL_ROW_TEMP_UP, CTRL_ROW_TEMP_DOWN, CTRL_ROW_FRUNK,
    CTRL_ROW_TRUNK, CTRL_ROW_CHARGE_PORT,
  };
  for (size_t i = 0; i < sizeof(head) / sizeof(head[0]) && n < max; i++)
    rows[n++] = head[i];
  // Charging group, kept next to Charge Port. Start/Stop only when plugged in;
  // the limit can be nudged whenever (it just won't take effect until plugged).
  if (charge_is_connected(s) && n < max) rows[n++] = CTRL_ROW_CHARGE;
  if (n < max) rows[n++] = CTRL_ROW_LIMIT_UP;
  if (n < max) rows[n++] = CTRL_ROW_LIMIT_DOWN;
  if (n < max) rows[n++] = CTRL_ROW_REFRESH;
  return n;
}

const char *controls_row_label(ControlsRow row) {
  switch (row) {
    case CTRL_ROW_WAKE:        return "Wake";
    case CTRL_ROW_TEMP_UP:     return "Temp +1°";
    case CTRL_ROW_TEMP_DOWN:   return "Temp -1°";
    case CTRL_ROW_FRUNK:       return "Open Frunk";
    case CTRL_ROW_TRUNK:       return "Open Trunk";
    case CTRL_ROW_CHARGE_PORT: return "Charge Port";
    case CTRL_ROW_CHARGE:      return "Start Charging"; // dynamic; see charge_toggle_label
    case CTRL_ROW_LIMIT_UP:    return "Limit +5%";
    case CTRL_ROW_LIMIT_DOWN:  return "Limit -5%";
    case CTRL_ROW_REFRESH:     return "Refresh";
  }
  return "";
}

int controls_row_cmd(ControlsRow row) {
  switch (row) {
    case CTRL_ROW_WAKE:        return CMD_WAKE;
    case CTRL_ROW_TEMP_UP:     return CMD_TEMP_UP;
    case CTRL_ROW_TEMP_DOWN:   return CMD_TEMP_DOWN;
    case CTRL_ROW_FRUNK:       return CMD_FRUNK;
    case CTRL_ROW_TRUNK:       return CMD_TRUNK;
    case CTRL_ROW_CHARGE_PORT: return CMD_CHARGE_PORT;
    case CTRL_ROW_CHARGE:      return CMD_CHARGE_START; // dynamic; see charge_toggle_cmd
    case CTRL_ROW_LIMIT_UP:    return CMD_LIMIT_UP;
    case CTRL_ROW_LIMIT_DOWN:  return CMD_LIMIT_DOWN;
    case CTRL_ROW_REFRESH:     return CMD_REFRESH;
  }
  return CMD_REFRESH;
}

int controls_row_temp_delta(ControlsRow row) {
  if (row == CTRL_ROW_TEMP_UP)   return  1;
  if (row == CTRL_ROW_TEMP_DOWN) return -1;
  return 0;
}

bool charge_is_connected(const VehicleState *s) {
  return s->charging == CHARGE_CHARGING ||
         s->charging == CHARGE_STOPPED  ||
         s->charging == CHARGE_COMPLETE;
}

const char *charge_toggle_label(const VehicleState *s) {
  return s->charging == CHARGE_CHARGING ? "Stop Charging" : "Start Charging";
}

int charge_toggle_cmd(const VehicleState *s) {
  return s->charging == CHARGE_CHARGING ? CMD_CHARGE_STOP : CMD_CHARGE_START;
}

void fmt_battery_subtitle(const VehicleState *s, char *out, size_t n) {
  if (s->battery >= 0)
    snprintf(out, n, "%d%%  •  %d mi", s->battery, s->range);
  else
    snprintf(out, n, "—");
}

void fmt_battery_pct(const VehicleState *s, char *out, size_t n) {
  if (s->battery >= 0)
    snprintf(out, n, "%d%%", s->battery);
  else
    snprintf(out, n, "—");
}

void fmt_battery_num(const VehicleState *s, char *out, size_t n) {
  if (s->battery >= 0)
    snprintf(out, n, "%d", s->battery);
  else
    snprintf(out, n, "—");
}

void fmt_range(const VehicleState *s, char *out, size_t n) {
  if (s->range >= 0)
    snprintf(out, n, "%d %s", s->range, s->dist_km ? "km" : "mi");
  else
    snprintf(out, n, "%s", "");
}

BatteryLevel battery_level(int pct) {
  if (pct < 0)   return BATTERY_UNKNOWN;
  if (pct <= 20) return BATTERY_LOW;
  if (pct <= 50) return BATTERY_MED;
  return BATTERY_HIGH;
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

bool charge_show_status(const VehicleState *s) {
  return s->charging == CHARGE_CHARGING || s->charging == CHARGE_COMPLETE;
}

void fmt_charge_subtitle(const VehicleState *s, char *out, size_t n) {
  if (s->charging == CHARGE_COMPLETE) {
    snprintf(out, n, "Charge complete");
  } else if (s->charging == CHARGE_CHARGING) {
    int m = s->charge_eta;
    if (m > 0) {
      int h = m / 60, mm = m % 60;
      if (h > 0) snprintf(out, n, "Charging · %dh%02dm", h, mm);
      else       snprintf(out, n, "Charging · %dm", mm);
    } else {
      snprintf(out, n, "Charging");
    }
  } else {
    snprintf(out, n, "%s", "");
  }
}

bool status_is_confirmation(const char *msg) {
  if (!msg || !msg[0]) return false;
  size_t len = strlen(msg);
  // In-progress strings end with the UTF-8 ellipsis "…" (E2 80 A6); those are
  // not confirmations (no success buzz until the command actually lands).
  if (len >= 3 &&
      (unsigned char)msg[len - 3] == 0xE2 &&
      (unsigned char)msg[len - 2] == 0x80 &&
      (unsigned char)msg[len - 1] == 0xA6) {
    return false;
  }
  return true;
}

const char *status_header_text(const char *name) {
  return (name && name[0]) ? name : "Status";
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

IconKind lock_toggle_icon(const VehicleState *s) {
  return s->locked ? ICON_KIND_LOCKED : ICON_KIND_UNLOCKED;
}

IconKind climate_toggle_icon(const VehicleState *s) {
  return s->climate_on ? ICON_KIND_CLIMATE_ON : ICON_KIND_CLIMATE_OFF;
}
