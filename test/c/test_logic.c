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
  RUN_TEST(test_awake_label);
  RUN_TEST(test_power_subtitle);
  RUN_TEST(test_small_buffer_is_null_terminated);
  return UNITY_END();
}
