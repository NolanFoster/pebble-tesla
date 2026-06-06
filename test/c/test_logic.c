// Host-side unit tests for src/c/logic.c (no Pebble SDK required).

#include "unity.h"
#include "logic.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static VehicleState base(void) {
  VehicleState s = { .battery = 84, .range = 240, .inside_temp = 21,
                     .target_temp = 22, .locked = true, .climate_on = true,
                     .online = true, .awake = AWAKE_AWAKE };
  return s;
}

void test_cmd_has_temp_delta(void) {
  TEST_ASSERT_TRUE(cmd_has_temp_delta(CMD_TEMP_UP));
  TEST_ASSERT_TRUE(cmd_has_temp_delta(CMD_TEMP_DOWN));
  TEST_ASSERT_FALSE(cmd_has_temp_delta(CMD_REFRESH));
  TEST_ASSERT_FALSE(cmd_has_temp_delta(CMD_LOCK));
  TEST_ASSERT_FALSE(cmd_has_temp_delta(CMD_CHARGE_PORT));
}

void test_battery_subtitle(void) {
  char buf[40];
  VehicleState s = base();
  fmt_battery_subtitle(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("84%  •  240 mi", buf);

  s.battery = -1;
  fmt_battery_subtitle(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("—", buf);
}

void test_lock_subtitle(void) {
  char buf[40];
  VehicleState s = base();
  s.locked = true;
  fmt_lock_subtitle(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("Locked", buf);
  s.locked = false;
  fmt_lock_subtitle(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("Unlocked", buf);
}

void test_climate_subtitle(void) {
  char buf[40];
  VehicleState s = base();
  s.climate_on = true;
  fmt_climate_subtitle(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("On  •  in 21° → 22°", buf);
  s.climate_on = false;
  fmt_climate_subtitle(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("Off  •  in 21°", buf);
}

void test_toggle_labels_and_commands(void) {
  VehicleState s = base();

  s.locked = true;
  TEST_ASSERT_EQUAL_STRING("Unlock", lock_toggle_label(&s));
  TEST_ASSERT_EQUAL_INT(CMD_UNLOCK, lock_toggle_cmd(&s));
  s.locked = false;
  TEST_ASSERT_EQUAL_STRING("Lock", lock_toggle_label(&s));
  TEST_ASSERT_EQUAL_INT(CMD_LOCK, lock_toggle_cmd(&s));

  s.climate_on = true;
  TEST_ASSERT_EQUAL_STRING("Climate Off", climate_toggle_label(&s));
  TEST_ASSERT_EQUAL_INT(CMD_CLIMATE_OFF, climate_toggle_cmd(&s));
  s.climate_on = false;
  TEST_ASSERT_EQUAL_STRING("Climate On", climate_toggle_label(&s));
  TEST_ASSERT_EQUAL_INT(CMD_CLIMATE_ON, climate_toggle_cmd(&s));
}

void test_battery_pct_and_range(void) {
  char buf[16];
  VehicleState s = base();

  fmt_battery_pct(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("84%", buf);
  fmt_range(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("240 mi", buf);

  s.dist_km = true;                 // honor the km/mi preference
  fmt_range(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("240 km", buf);
  s.dist_km = false;

  s.battery = -1;
  fmt_battery_pct(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("—", buf);
  s.range = -1;
  fmt_range(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_battery_num(void) {
  char buf[16];
  VehicleState s = base();

  s.battery = 84;
  fmt_battery_num(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("84", buf);
  s.battery = 0;
  fmt_battery_num(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0", buf);
  s.battery = 100;
  fmt_battery_num(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("100", buf);
  s.battery = -1;
  fmt_battery_num(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("—", buf);
}

void test_battery_level(void) {
  TEST_ASSERT_EQUAL_INT(BATTERY_UNKNOWN, battery_level(-1));
  TEST_ASSERT_EQUAL_INT(BATTERY_LOW,     battery_level(0));
  TEST_ASSERT_EQUAL_INT(BATTERY_LOW,     battery_level(20));
  TEST_ASSERT_EQUAL_INT(BATTERY_MED,     battery_level(21));
  TEST_ASSERT_EQUAL_INT(BATTERY_MED,     battery_level(50));
  TEST_ASSERT_EQUAL_INT(BATTERY_HIGH,    battery_level(51));
  TEST_ASSERT_EQUAL_INT(BATTERY_HIGH,    battery_level(100));
}

void test_toggle_icons(void) {
  VehicleState s = base();

  s.locked = true;
  TEST_ASSERT_EQUAL_INT(ICON_KIND_LOCKED, lock_toggle_icon(&s));
  s.locked = false;
  TEST_ASSERT_EQUAL_INT(ICON_KIND_UNLOCKED, lock_toggle_icon(&s));

  s.climate_on = true;
  TEST_ASSERT_EQUAL_INT(ICON_KIND_CLIMATE_ON, climate_toggle_icon(&s));
  s.climate_on = false;
  TEST_ASSERT_EQUAL_INT(ICON_KIND_CLIMATE_OFF, climate_toggle_icon(&s));
}

void test_awake_label(void) {
  TEST_ASSERT_EQUAL_STRING("Awake", awake_label(AWAKE_AWAKE));
  TEST_ASSERT_EQUAL_STRING("Asleep", awake_label(AWAKE_ASLEEP));
  TEST_ASSERT_EQUAL_STRING("Waiting for sleep", awake_label(AWAKE_WAITING));
  TEST_ASSERT_EQUAL_STRING("—", awake_label(AWAKE_UNKNOWN));
  TEST_ASSERT_EQUAL_STRING("—", awake_label(99));
}

void test_power_subtitle(void) {
  char buf[40];
  VehicleState s = base();
  s.awake = AWAKE_AWAKE;
  fmt_power_subtitle(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("Awake", buf);
  s.awake = AWAKE_WAITING;
  fmt_power_subtitle(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("Waiting for sleep", buf);
}

void test_controls_menu_rows_hides_wake_when_awake(void) {
  ControlsRow rows[CONTROLS_MAX_ROWS];
  VehicleState s = base();  // awake, disconnected (no charge toggle)
  int n = controls_menu_rows(&s, rows, CONTROLS_MAX_ROWS);
  TEST_ASSERT_EQUAL_INT(8, n);                        // 5 head + Limit± + Refresh
  TEST_ASSERT_EQUAL_INT(CTRL_ROW_TEMP_UP, rows[0]);   // no Wake row
  TEST_ASSERT_EQUAL_INT(CTRL_ROW_REFRESH, rows[7]);

  // Same for unknown / waiting-for-sleep: Wake only shows when asleep.
  s.awake = AWAKE_UNKNOWN;
  TEST_ASSERT_EQUAL_INT(8, controls_menu_rows(&s, rows, CONTROLS_MAX_ROWS));
  TEST_ASSERT_EQUAL_INT(CTRL_ROW_TEMP_UP, rows[0]);
  s.awake = AWAKE_WAITING;
  TEST_ASSERT_EQUAL_INT(8, controls_menu_rows(&s, rows, CONTROLS_MAX_ROWS));
}

void test_controls_menu_rows_shows_wake_when_asleep(void) {
  ControlsRow rows[CONTROLS_MAX_ROWS];
  VehicleState s = base();
  s.awake = AWAKE_ASLEEP;
  int n = controls_menu_rows(&s, rows, CONTROLS_MAX_ROWS);
  TEST_ASSERT_EQUAL_INT(9, n);                        // Wake + 8
  TEST_ASSERT_EQUAL_INT(CTRL_ROW_WAKE, rows[0]);      // Wake is first
  TEST_ASSERT_EQUAL_INT(CTRL_ROW_TEMP_UP, rows[1]);
  TEST_ASSERT_EQUAL_INT(CTRL_ROW_REFRESH, rows[8]);
}

void test_controls_menu_rows_shows_charge_toggle_when_plugged_in(void) {
  ControlsRow rows[CONTROLS_MAX_ROWS];
  VehicleState s = base();          // awake
  s.charging = CHARGE_CHARGING;     // plugged in -> charge toggle present
  int n = controls_menu_rows(&s, rows, CONTROLS_MAX_ROWS);
  TEST_ASSERT_EQUAL_INT(9, n);                        // 5 head + Charge + Limit± + Refresh
  TEST_ASSERT_EQUAL_INT(CTRL_ROW_CHARGE_PORT, rows[4]);
  TEST_ASSERT_EQUAL_INT(CTRL_ROW_CHARGE, rows[5]);    // right after Charge Port
  TEST_ASSERT_EQUAL_INT(CTRL_ROW_LIMIT_UP, rows[6]);
  TEST_ASSERT_EQUAL_INT(CTRL_ROW_REFRESH, rows[8]);

  // Asleep + plugged in fills every row (Wake + the charge toggle).
  s.awake = AWAKE_ASLEEP;
  TEST_ASSERT_EQUAL_INT(CONTROLS_MAX_ROWS,
                        controls_menu_rows(&s, rows, CONTROLS_MAX_ROWS));

  // Disconnected hides the toggle again.
  s.awake = AWAKE_AWAKE;
  s.charging = CHARGE_DISCONNECTED;
  TEST_ASSERT_EQUAL_INT(8, controls_menu_rows(&s, rows, CONTROLS_MAX_ROWS));
}

void test_charge_toggle_and_connected(void) {
  VehicleState s = base();

  s.charging = CHARGE_CHARGING;
  TEST_ASSERT_TRUE(charge_is_connected(&s));
  TEST_ASSERT_EQUAL_STRING("Stop Charging", charge_toggle_label(&s));
  TEST_ASSERT_EQUAL_INT(CMD_CHARGE_STOP, charge_toggle_cmd(&s));

  s.charging = CHARGE_STOPPED;
  TEST_ASSERT_TRUE(charge_is_connected(&s));
  TEST_ASSERT_EQUAL_STRING("Start Charging", charge_toggle_label(&s));
  TEST_ASSERT_EQUAL_INT(CMD_CHARGE_START, charge_toggle_cmd(&s));

  s.charging = CHARGE_COMPLETE;
  TEST_ASSERT_TRUE(charge_is_connected(&s));

  s.charging = CHARGE_DISCONNECTED;
  TEST_ASSERT_FALSE(charge_is_connected(&s));
  s.charging = CHARGE_UNKNOWN;
  TEST_ASSERT_FALSE(charge_is_connected(&s));
}

void test_charge_subtitle(void) {
  char buf[40];
  VehicleState s = base();

  s.charging = CHARGE_CHARGING; s.charge_eta = 80;   // 1h20m
  TEST_ASSERT_TRUE(charge_show_status(&s));
  fmt_charge_subtitle(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("Charging · 1h20m", buf);

  s.charge_eta = 45;                                 // under an hour
  fmt_charge_subtitle(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("Charging · 45m", buf);

  s.charge_eta = -1;                                 // unknown eta
  fmt_charge_subtitle(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("Charging", buf);

  s.charging = CHARGE_COMPLETE;
  TEST_ASSERT_TRUE(charge_show_status(&s));
  fmt_charge_subtitle(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("Charge complete", buf);

  s.charging = CHARGE_STOPPED;                       // plugged but idle -> power footer
  TEST_ASSERT_FALSE(charge_show_status(&s));
  fmt_charge_subtitle(&s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("", buf);

  s.charging = CHARGE_DISCONNECTED;
  TEST_ASSERT_FALSE(charge_show_status(&s));
}

void test_controls_row_label_cmd_limit(void) {
  TEST_ASSERT_EQUAL_STRING("Limit +5%", controls_row_label(CTRL_ROW_LIMIT_UP));
  TEST_ASSERT_EQUAL_STRING("Limit -5%", controls_row_label(CTRL_ROW_LIMIT_DOWN));
  TEST_ASSERT_EQUAL_INT(CMD_LIMIT_UP, controls_row_cmd(CTRL_ROW_LIMIT_UP));
  TEST_ASSERT_EQUAL_INT(CMD_LIMIT_DOWN, controls_row_cmd(CTRL_ROW_LIMIT_DOWN));
  TEST_ASSERT_EQUAL_INT(0, controls_row_temp_delta(CTRL_ROW_LIMIT_UP));
}

void test_status_is_confirmation(void) {
  TEST_ASSERT_TRUE(status_is_confirmation("Locked"));
  TEST_ASSERT_TRUE(status_is_confirmation("Set 22°C"));
  TEST_ASSERT_TRUE(status_is_confirmation("Limit 85%"));
  TEST_ASSERT_FALSE(status_is_confirmation("Waking…"));     // ends with ellipsis
  TEST_ASSERT_FALSE(status_is_confirmation("Locking…"));
  TEST_ASSERT_FALSE(status_is_confirmation(""));
  TEST_ASSERT_FALSE(status_is_confirmation(NULL));
}

void test_controls_row_label_cmd_delta(void) {
  TEST_ASSERT_EQUAL_STRING("Wake", controls_row_label(CTRL_ROW_WAKE));
  TEST_ASSERT_EQUAL_STRING("Temp +1°", controls_row_label(CTRL_ROW_TEMP_UP));
  TEST_ASSERT_EQUAL_STRING("Charge Port", controls_row_label(CTRL_ROW_CHARGE_PORT));

  TEST_ASSERT_EQUAL_INT(CMD_WAKE, controls_row_cmd(CTRL_ROW_WAKE));
  TEST_ASSERT_EQUAL_INT(CMD_TEMP_DOWN, controls_row_cmd(CTRL_ROW_TEMP_DOWN));
  TEST_ASSERT_EQUAL_INT(CMD_REFRESH, controls_row_cmd(CTRL_ROW_REFRESH));

  TEST_ASSERT_EQUAL_INT(1,  controls_row_temp_delta(CTRL_ROW_TEMP_UP));
  TEST_ASSERT_EQUAL_INT(-1, controls_row_temp_delta(CTRL_ROW_TEMP_DOWN));
  TEST_ASSERT_EQUAL_INT(0,  controls_row_temp_delta(CTRL_ROW_WAKE));
  TEST_ASSERT_EQUAL_INT(0,  controls_row_temp_delta(CTRL_ROW_FRUNK));
}

void test_status_header_text(void) {
  TEST_ASSERT_EQUAL_STRING("Status", status_header_text(NULL));
  TEST_ASSERT_EQUAL_STRING("Status", status_header_text(""));
  TEST_ASSERT_EQUAL_STRING("Bumblebee", status_header_text("Bumblebee"));
}

void test_small_buffer_is_null_terminated(void) {
  char buf[8];
  VehicleState s = base();
  fmt_battery_subtitle(&s, buf, sizeof(buf)); // would overflow without snprintf bound
  TEST_ASSERT_EQUAL_CHAR('\0', buf[sizeof(buf) - 1]);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_cmd_has_temp_delta);
  RUN_TEST(test_battery_subtitle);
  RUN_TEST(test_lock_subtitle);
  RUN_TEST(test_climate_subtitle);
  RUN_TEST(test_toggle_labels_and_commands);
  RUN_TEST(test_battery_pct_and_range);
  RUN_TEST(test_battery_num);
  RUN_TEST(test_battery_level);
  RUN_TEST(test_toggle_icons);
  RUN_TEST(test_awake_label);
  RUN_TEST(test_power_subtitle);
  RUN_TEST(test_controls_menu_rows_hides_wake_when_awake);
  RUN_TEST(test_controls_menu_rows_shows_wake_when_asleep);
  RUN_TEST(test_controls_menu_rows_shows_charge_toggle_when_plugged_in);
  RUN_TEST(test_charge_toggle_and_connected);
  RUN_TEST(test_charge_subtitle);
  RUN_TEST(test_controls_row_label_cmd_limit);
  RUN_TEST(test_status_is_confirmation);
  RUN_TEST(test_controls_row_label_cmd_delta);
  RUN_TEST(test_status_header_text);
  RUN_TEST(test_small_buffer_is_null_terminated);
  return UNITY_END();
}
