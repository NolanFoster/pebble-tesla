#include <pebble.h>
#include "logic.h"

// ---- Message keys (must match package.json messageKeys) ----
#define KEY_CMD          MESSAGE_KEY_CMD
#define KEY_STATUS       MESSAGE_KEY_STATUS
#define KEY_BATTERY      MESSAGE_KEY_BATTERY
#define KEY_RANGE        MESSAGE_KEY_RANGE
#define KEY_LOCKED       MESSAGE_KEY_LOCKED
#define KEY_CLIMATE_ON   MESSAGE_KEY_CLIMATE_ON
#define KEY_INSIDE_TEMP  MESSAGE_KEY_INSIDE_TEMP
#define KEY_TARGET_TEMP  MESSAGE_KEY_TARGET_TEMP
#define KEY_ONLINE       MESSAGE_KEY_ONLINE
#define KEY_AWAKE        MESSAGE_KEY_AWAKE
#define KEY_NAME         MESSAGE_KEY_NAME
#define KEY_ERROR        MESSAGE_KEY_ERROR
#define KEY_TEMP_DELTA   MESSAGE_KEY_TEMP_DELTA

// ---- Command codes (TeslaCommand enum) come from logic.h ----

// ---- Cached vehicle state ----
static int      s_battery     = -1;
static int      s_range       = -1;
static bool     s_locked      = true;
static bool     s_climate_on  = false;
static int      s_inside_temp = 0;
static int      s_target_temp = 0;
static bool     s_online      = false;
static int      s_awake       = AWAKE_UNKNOWN;
static char     s_name[100]   = "";   // vehicle name (UTF-8; room for ~24 emoji)
static char     s_error[64]   = "";

// ---- UI ----
static Window         *s_main_window;
static Layer          *s_card_layer;     // custom-drawn status "card" (left of action bar)
static ActionBarLayer *s_action_bar;     // lock (up) / settings (select) / climate (down)
static GBitmap        *s_icon_locked, *s_icon_unlocked, *s_icon_settings;
static GBitmap        *s_icon_climate_on, *s_icon_climate_off;

static Window      *s_controls_window;   // "More Controls" sub-window (select button)
static MenuLayer   *s_controls_menu;

static Window      *s_status_window;     // transient "Sending…" / result overlay
static TextLayer   *s_status_text;
static char         s_status_buf[64];
static AppTimer    *s_status_timer;

// Forward decls
static void send_command(TeslaCommand cmd, int arg);
static void update_action_bar_icons(void);
static void push_controls_window(void);

// ---------------------------------------------------------------------------
// Transient status overlay
// ---------------------------------------------------------------------------
static void status_window_unload(Window *w) {
  text_layer_destroy(s_status_text);
  s_status_text = NULL;
  s_status_window = NULL;
}

static void status_window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_status_text = text_layer_create(GRect(0, (b.size.h - 40) / 2, b.size.w, 40));
  text_layer_set_text(s_status_text, s_status_buf);
  text_layer_set_font(s_status_text, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_status_text, GTextAlignmentCenter);
  text_layer_set_background_color(s_status_text, GColorClear);
  layer_add_child(root, text_layer_get_layer(s_status_text));
}

static void dismiss_status(void *ctx) {
  s_status_timer = NULL;
  if (s_status_window) {
    window_stack_remove(s_status_window, true);
    window_destroy(s_status_window);
    s_status_window = NULL;
  }
}

static void show_status(const char *msg, uint32_t dismiss_ms) {
  strncpy(s_status_buf, msg, sizeof(s_status_buf) - 1);
  s_status_buf[sizeof(s_status_buf) - 1] = '\0';

  if (!s_status_window) {
    s_status_window = window_create();
    window_set_window_handlers(s_status_window, (WindowHandlers){
      .load = status_window_load,
      .unload = status_window_unload,
    });
    window_stack_push(s_status_window, true);
  } else if (s_status_text) {
    text_layer_set_text(s_status_text, s_status_buf);
  }

  if (s_status_timer) { app_timer_cancel(s_status_timer); s_status_timer = NULL; }
  if (dismiss_ms > 0) {
    s_status_timer = app_timer_register(dismiss_ms, dismiss_status, NULL);
  }
}

// Snapshot the cached globals into a VehicleState for the pure logic helpers.
static VehicleState current_state(void) {
  return (VehicleState){
    .battery = s_battery, .range = s_range,
    .inside_temp = s_inside_temp, .target_temp = s_target_temp,
    .locked = s_locked, .climate_on = s_climate_on, .online = s_online,
    .awake = s_awake,
  };
}

// ---------------------------------------------------------------------------
// Status card (left of the action bar)
// ---------------------------------------------------------------------------
// Draws a single screenful of vehicle status: name, battery+range, lock state,
// climate state and power. All strings come from the shared logic.c formatters
// so the card stays byte-identical to the old menu rows.
static void card_update_proc(Layer *layer, GContext *gctx) {
  GRect b = layer_get_bounds(layer);
  VehicleState st = current_state();

  GColor fg = PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack);
  graphics_context_set_text_color(gctx, fg);

  const GTextAlignment align = PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft);
  const int16_t pad = PBL_IF_ROUND_ELSE(0, 6);
  GRect content = GRect(b.origin.x + pad, b.origin.y,
                        b.size.w - 2 * pad, b.size.h);

  char line[40];
  GFont f_name = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GFont f_body = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  GFont f_small = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  // Top inset is larger on round to dodge the curved corners.
  int16_t y = PBL_IF_ROUND_ELSE(28, 6);

  // Vehicle name (or "Status").
  graphics_draw_text(gctx, status_header_text(s_name), f_name,
    GRect(content.origin.x, y, content.size.w, 30),
    GTextOverflowModeTrailingEllipsis, align, NULL);
  y += 32;

  // Battery + range.
  fmt_battery_subtitle(&st, line, sizeof(line));
  graphics_draw_text(gctx, line, f_body,
    GRect(content.origin.x, y, content.size.w, 24),
    GTextOverflowModeTrailingEllipsis, align, NULL);
  y += 26;

  // Doors (lock state).
  fmt_lock_subtitle(&st, line, sizeof(line));
  graphics_draw_text(gctx, line, f_body,
    GRect(content.origin.x, y, content.size.w, 24),
    GTextOverflowModeTrailingEllipsis, align, NULL);
  y += 26;

  // Climate (may be longer: "On  •  in 21° → 22°").
  fmt_climate_subtitle(&st, line, sizeof(line));
  graphics_draw_text(gctx, line, f_body,
    GRect(content.origin.x, y, content.size.w, 24),
    GTextOverflowModeTrailingEllipsis, align, NULL);
  y += 26;

  // Power/awake (small, footer).
  fmt_power_subtitle(&st, line, sizeof(line));
  graphics_draw_text(gctx, line, f_small,
    GRect(content.origin.x, y, content.size.w, 18),
    GTextOverflowModeTrailingEllipsis, align, NULL);
}

// ---------------------------------------------------------------------------
// Action bar (lock = up, settings = select, climate = down)
// ---------------------------------------------------------------------------
static void ab_up_click(ClickRecognizerRef rec, void *ctx) {
  VehicleState st = current_state();
  send_command(lock_toggle_cmd(&st), 0);
  show_status(s_locked ? "Unlocking…" : "Locking…", 0);
}

static void ab_down_click(ClickRecognizerRef rec, void *ctx) {
  VehicleState st = current_state();
  send_command(climate_toggle_cmd(&st), 0);
  show_status(s_climate_on ? "Climate off…" : "Climate on…", 0);
}

static void ab_select_click(ClickRecognizerRef rec, void *ctx) {
  push_controls_window();
}

static void action_bar_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP,     ab_up_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, ab_select_click);
  window_single_click_subscribe(BUTTON_ID_DOWN,   ab_down_click);
}

// Refresh the up/down glyphs to mirror the current lock/climate state.
static void update_action_bar_icons(void) {
  if (!s_action_bar) return;
  VehicleState st = current_state();
  GBitmap *up = (lock_toggle_icon(&st) == ICON_KIND_LOCKED) ? s_icon_locked
                                                            : s_icon_unlocked;
  GBitmap *down = (climate_toggle_icon(&st) == ICON_KIND_CLIMATE_ON) ? s_icon_climate_on
                                                                     : s_icon_climate_off;
  action_bar_layer_set_icon(s_action_bar, BUTTON_ID_UP, up);
  action_bar_layer_set_icon(s_action_bar, BUTTON_ID_SELECT, s_icon_settings);
  action_bar_layer_set_icon(s_action_bar, BUTTON_ID_DOWN, down);
}

// ---------------------------------------------------------------------------
// "More Controls" sub-window (opened from the SELECT button)
// ---------------------------------------------------------------------------
enum {
  MC_TEMP_UP = 0,
  MC_TEMP_DOWN,
  MC_FRUNK,
  MC_TRUNK,
  MC_CHARGE_PORT,
  MC_REFRESH,
  MC_COUNT
};

static uint16_t mc_num_rows(MenuLayer *ml, uint16_t section, void *ctx) {
  return MC_COUNT;
}

static int16_t mc_header_height(MenuLayer *ml, uint16_t section, void *ctx) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void mc_draw_header(GContext *gctx, const Layer *cell, uint16_t section, void *ctx) {
  menu_cell_basic_header_draw(gctx, cell, "More Controls");
}

static void mc_draw_row(GContext *gctx, const Layer *cell, MenuIndex *idx, void *ctx) {
  const char *title = "";
  switch (idx->row) {
    case MC_TEMP_UP:     title = "Temp +1°";    break;
    case MC_TEMP_DOWN:   title = "Temp -1°";    break;
    case MC_FRUNK:       title = "Open Frunk";  break;
    case MC_TRUNK:       title = "Open Trunk";  break;
    case MC_CHARGE_PORT: title = "Charge Port"; break;
    case MC_REFRESH:     title = "Refresh";     break;
  }
  menu_cell_basic_draw(gctx, cell, title, NULL, NULL);
}

static void mc_select(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  switch (idx->row) {
    case MC_TEMP_UP:
      send_command(CMD_TEMP_UP, 1);
      show_status("Temp +1°…", 0);
      break;
    case MC_TEMP_DOWN:
      send_command(CMD_TEMP_DOWN, -1);
      show_status("Temp -1°…", 0);
      break;
    case MC_FRUNK:
      send_command(CMD_FRUNK, 0);
      show_status("Opening frunk…", 0);
      break;
    case MC_TRUNK:
      send_command(CMD_TRUNK, 0);
      show_status("Opening trunk…", 0);
      break;
    case MC_CHARGE_PORT:
      send_command(CMD_CHARGE_PORT, 0);
      show_status("Charge port…", 0);
      break;
    case MC_REFRESH:
      send_command(CMD_REFRESH, 0);
      show_status("Refreshing…", 0);
      break;
  }
}

static void controls_window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_controls_menu = menu_layer_create(b);
  menu_layer_set_callbacks(s_controls_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = mc_num_rows,
    .get_header_height = mc_header_height,
    .draw_header = mc_draw_header,
    .draw_row = mc_draw_row,
    .select_click = mc_select,
  });
  menu_layer_set_click_config_onto_window(s_controls_menu, w);
#if defined(PBL_COLOR)
  menu_layer_set_highlight_colors(s_controls_menu, GColorRed, GColorWhite);
#endif
  layer_add_child(root, menu_layer_get_layer(s_controls_menu));
}

static void controls_window_unload(Window *w) {
  menu_layer_destroy(s_controls_menu);
  s_controls_menu = NULL;
}

static void push_controls_window(void) {
  if (!s_controls_window) {
    s_controls_window = window_create();
    window_set_window_handlers(s_controls_window, (WindowHandlers){
      .load = controls_window_load,
      .unload = controls_window_unload,
    });
  }
  window_stack_push(s_controls_window, true);
}

// ---------------------------------------------------------------------------
// AppMessage
// ---------------------------------------------------------------------------
static void send_command(TeslaCommand cmd, int arg) {
  DictionaryIterator *it;
  AppMessageResult r = app_message_outbox_begin(&it);
  if (r != APP_MSG_OK) {
    show_status("Phone busy", 1500);
    return;
  }
  // TeslaCommand is a narrow enum: arm-none-eabi-gcc defaults to -fshort-enums,
  // so `cmd` occupies 1 byte. Writing &cmd with sizeof(int)=4 would read 3 bytes
  // of adjacent stack garbage into the high bytes (the watch sent 2 but the phone
  // saw 0xE1980002). Copy into a real int32 whose width matches the requested size.
  int32_t cmd_val = (int32_t)cmd;
  dict_write_int(it, KEY_CMD, &cmd_val, sizeof(cmd_val), true);
  if (cmd_has_temp_delta(cmd)) {
    dict_write_int(it, KEY_TEMP_DELTA, &arg, sizeof(arg), true);
  }
  app_message_outbox_send();
}

static void inbox_received(DictionaryIterator *it, void *ctx) {
  Tuple *t;
  bool got_state = false;

  if ((t = dict_find(it, KEY_ERROR))) {
    strncpy(s_error, t->value->cstring, sizeof(s_error) - 1);
    s_error[sizeof(s_error) - 1] = '\0';
    show_status(s_error, 2500);
    return;
  }
  if ((t = dict_find(it, KEY_STATUS))) {
    // A short human string confirming a command landed
    show_status(t->value->cstring, 1500);
  }
  if ((t = dict_find(it, KEY_BATTERY)))     { s_battery = t->value->int32; got_state = true; }
  if ((t = dict_find(it, KEY_RANGE)))       { s_range = t->value->int32; got_state = true; }
  if ((t = dict_find(it, KEY_LOCKED)))      { s_locked = t->value->int32 != 0; got_state = true; }
  if ((t = dict_find(it, KEY_CLIMATE_ON)))  { s_climate_on = t->value->int32 != 0; got_state = true; }
  if ((t = dict_find(it, KEY_INSIDE_TEMP))) { s_inside_temp = t->value->int32; got_state = true; }
  if ((t = dict_find(it, KEY_TARGET_TEMP))) { s_target_temp = t->value->int32; got_state = true; }
  if ((t = dict_find(it, KEY_ONLINE)))      { s_online = t->value->int32 != 0; got_state = true; }
  if ((t = dict_find(it, KEY_AWAKE)))       { s_awake = t->value->int32; got_state = true; }
  if ((t = dict_find(it, KEY_NAME))) {
    strncpy(s_name, t->value->cstring, sizeof(s_name) - 1);
    s_name[sizeof(s_name) - 1] = '\0';
    got_state = true;
  }

  if (got_state) {
    if (s_card_layer) layer_mark_dirty(s_card_layer);
    update_action_bar_icons();                        // lock/climate glyph follows state
    if (s_controls_menu) menu_layer_reload_data(s_controls_menu);
    // A fresh state read means any in-flight "Refreshing…/…ing" overlay is done.
    // Refresh replies carry no STATUS, so without this the overlay (shown with
    // dismiss_ms=0) would stay up forever and swallow further button presses.
    if (s_status_timer) { app_timer_cancel(s_status_timer); s_status_timer = NULL; }
    dismiss_status(NULL);
  }
}

static void inbox_dropped(AppMessageResult reason, void *ctx) {
  show_status("Msg dropped", 1500);
}

static void outbox_failed(DictionaryIterator *it, AppMessageResult reason, void *ctx) {
  show_status("Send failed", 1500);
}

static void outbox_sent(DictionaryIterator *it, void *ctx) {
  // command reached the phone; await STATUS/state reply
}

// ---------------------------------------------------------------------------
// Main window
// ---------------------------------------------------------------------------
static void load_icons(void) {
  s_icon_locked      = gbitmap_create_with_resource(RESOURCE_ID_ICON_LOCKED);
  s_icon_unlocked    = gbitmap_create_with_resource(RESOURCE_ID_ICON_UNLOCKED);
  s_icon_settings    = gbitmap_create_with_resource(RESOURCE_ID_ICON_SETTINGS);
  s_icon_climate_on  = gbitmap_create_with_resource(RESOURCE_ID_ICON_CLIMATE_ON);
  s_icon_climate_off = gbitmap_create_with_resource(RESOURCE_ID_ICON_CLIMATE_OFF);
}

static void unload_icons(void) {
  gbitmap_destroy(s_icon_locked);      s_icon_locked = NULL;
  gbitmap_destroy(s_icon_unlocked);    s_icon_unlocked = NULL;
  gbitmap_destroy(s_icon_settings);    s_icon_settings = NULL;
  gbitmap_destroy(s_icon_climate_on);  s_icon_climate_on = NULL;
  gbitmap_destroy(s_icon_climate_off); s_icon_climate_off = NULL;
}

static void main_window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);

  window_set_background_color(w, PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite));

  // Status card fills everything except the action bar column (on rectangular
  // displays). On round the action bar overlaps the edge, so use full bounds.
  GRect card = PBL_IF_ROUND_ELSE(
    b,
    GRect(b.origin.x, b.origin.y, b.size.w - ACTION_BAR_WIDTH, b.size.h));
  s_card_layer = layer_create(card);
  layer_set_update_proc(s_card_layer, card_update_proc);
  layer_add_child(root, s_card_layer);

  s_action_bar = action_bar_layer_create();
  action_bar_layer_set_click_config_provider(s_action_bar, action_bar_click_config);
#if defined(PBL_COLOR)
  action_bar_layer_set_background_color(s_action_bar, GColorRed);
#endif
  action_bar_layer_add_to_window(s_action_bar, w);
  update_action_bar_icons();
}

static void main_window_unload(Window *w) {
  action_bar_layer_destroy(s_action_bar);
  s_action_bar = NULL;
  layer_destroy(s_card_layer);
  s_card_layer = NULL;
}

static void init(void) {
  load_icons();

  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers){
    .load = main_window_load,
    .unload = main_window_unload,
  });

  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_failed(outbox_failed);
  app_message_register_outbox_sent(outbox_sent);
  app_message_open(256, 128);

  window_stack_push(s_main_window, true);

  // Pull initial state once JS is ready (JS also auto-refreshes on 'ready')
  send_command(CMD_REFRESH, 0);
}

static void deinit(void) {
  if (s_controls_window) window_destroy(s_controls_window);
  window_destroy(s_main_window);
  unload_icons();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
