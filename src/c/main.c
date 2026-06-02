#include <pebble.h>

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
#define KEY_ERROR        MESSAGE_KEY_ERROR
#define KEY_TEMP_DELTA   MESSAGE_KEY_TEMP_DELTA

// ---- Command codes sent watch -> phone ----
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

// ---- Cached vehicle state ----
static int      s_battery     = -1;
static int      s_range       = -1;
static bool     s_locked      = true;
static bool     s_climate_on  = false;
static int      s_inside_temp = 0;
static int      s_target_temp = 0;
static bool     s_online      = false;
static char     s_error[64]   = "";

// ---- UI ----
static Window      *s_main_window;
static MenuLayer   *s_menu_layer;
static Window      *s_status_window;   // transient "Sending…" / result overlay
static TextLayer   *s_status_text;
static char         s_status_buf[64];
static AppTimer    *s_status_timer;

// Forward decls
static void send_command(TeslaCommand cmd, int arg);

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

// ---------------------------------------------------------------------------
// Menu model
// ---------------------------------------------------------------------------
// Section 0: status (read-only rows)   Section 1: actions
enum { SEC_STATUS = 0, SEC_ACTIONS = 1, NUM_SECTIONS };

enum {
  ROW_BATTERY = 0,
  ROW_LOCK_STATE,
  ROW_CLIMATE_STATE,
  NUM_STATUS_ROWS
};

enum {
  ACT_LOCK_TOGGLE = 0,
  ACT_CLIMATE_TOGGLE,
  ACT_TEMP_UP,
  ACT_TEMP_DOWN,
  ACT_FRUNK,
  ACT_TRUNK,
  ACT_CHARGE_PORT,
  ACT_REFRESH,
  NUM_ACTION_ROWS
};

static uint16_t menu_num_sections(MenuLayer *ml, void *ctx) {
  return NUM_SECTIONS;
}

static uint16_t menu_num_rows(MenuLayer *ml, uint16_t section, void *ctx) {
  return section == SEC_STATUS ? NUM_STATUS_ROWS : NUM_ACTION_ROWS;
}

static int16_t menu_header_height(MenuLayer *ml, uint16_t section, void *ctx) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void menu_draw_header(GContext *gctx, const Layer *cell, uint16_t section, void *ctx) {
  menu_cell_basic_header_draw(gctx, cell,
    section == SEC_STATUS ? "Status" : "Controls");
}

static void menu_draw_row(GContext *gctx, const Layer *cell,
                          MenuIndex *idx, void *ctx) {
  char title[40];
  char subtitle[40];
  title[0] = subtitle[0] = '\0';

  if (idx->section == SEC_STATUS) {
    switch (idx->row) {
      case ROW_BATTERY:
        snprintf(title, sizeof(title), "Battery");
        if (s_battery >= 0)
          snprintf(subtitle, sizeof(subtitle), "%d%%  •  %d mi", s_battery, s_range);
        else
          snprintf(subtitle, sizeof(subtitle), "—");
        break;
      case ROW_LOCK_STATE:
        snprintf(title, sizeof(title), "Doors");
        snprintf(subtitle, sizeof(subtitle), "%s", s_locked ? "Locked" : "Unlocked");
        break;
      case ROW_CLIMATE_STATE:
        snprintf(title, sizeof(title), "Climate");
        if (s_climate_on)
          snprintf(subtitle, sizeof(subtitle), "On  •  in %d° → %d°",
                   s_inside_temp, s_target_temp);
        else
          snprintf(subtitle, sizeof(subtitle), "Off  •  in %d°", s_inside_temp);
        break;
    }
    menu_cell_basic_draw(gctx, cell, title, subtitle, NULL);
    return;
  }

  // Actions
  switch (idx->row) {
    case ACT_LOCK_TOGGLE:    snprintf(title, sizeof(title), s_locked ? "Unlock" : "Lock"); break;
    case ACT_CLIMATE_TOGGLE: snprintf(title, sizeof(title), s_climate_on ? "Climate Off" : "Climate On"); break;
    case ACT_TEMP_UP:        snprintf(title, sizeof(title), "Temp +1°"); break;
    case ACT_TEMP_DOWN:      snprintf(title, sizeof(title), "Temp -1°"); break;
    case ACT_FRUNK:          snprintf(title, sizeof(title), "Open Frunk"); break;
    case ACT_TRUNK:          snprintf(title, sizeof(title), "Open Trunk"); break;
    case ACT_CHARGE_PORT:    snprintf(title, sizeof(title), "Charge Port"); break;
    case ACT_REFRESH:        snprintf(title, sizeof(title), "Refresh"); break;
  }
  menu_cell_basic_draw(gctx, cell, title, NULL, NULL);
}

static void menu_select(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  if (idx->section == SEC_STATUS) {
    // tapping a status row refreshes
    send_command(CMD_REFRESH, 0);
    show_status("Refreshing…", 0);
    return;
  }
  switch (idx->row) {
    case ACT_LOCK_TOGGLE:
      send_command(s_locked ? CMD_UNLOCK : CMD_LOCK, 0);
      show_status(s_locked ? "Unlocking…" : "Locking…", 0);
      break;
    case ACT_CLIMATE_TOGGLE:
      send_command(s_climate_on ? CMD_CLIMATE_OFF : CMD_CLIMATE_ON, 0);
      show_status(s_climate_on ? "Climate off…" : "Climate on…", 0);
      break;
    case ACT_TEMP_UP:
      send_command(CMD_TEMP_UP, 1);
      show_status("Temp +1°…", 0);
      break;
    case ACT_TEMP_DOWN:
      send_command(CMD_TEMP_DOWN, -1);
      show_status("Temp -1°…", 0);
      break;
    case ACT_FRUNK:
      send_command(CMD_FRUNK, 0);
      show_status("Opening frunk…", 0);
      break;
    case ACT_TRUNK:
      send_command(CMD_TRUNK, 0);
      show_status("Opening trunk…", 0);
      break;
    case ACT_CHARGE_PORT:
      send_command(CMD_CHARGE_PORT, 0);
      show_status("Charge port…", 0);
      break;
    case ACT_REFRESH:
      send_command(CMD_REFRESH, 0);
      show_status("Refreshing…", 0);
      break;
  }
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
  dict_write_int(it, KEY_CMD, &cmd, sizeof(int), true);
  if (cmd == CMD_TEMP_UP || cmd == CMD_TEMP_DOWN) {
    dict_write_int(it, KEY_TEMP_DELTA, &arg, sizeof(int), true);
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

  if (got_state && s_menu_layer) {
    menu_layer_reload_data(s_menu_layer);
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
static void main_window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);

  s_menu_layer = menu_layer_create(b);
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_sections = menu_num_sections,
    .get_num_rows = menu_num_rows,
    .get_header_height = menu_header_height,
    .draw_header = menu_draw_header,
    .draw_row = menu_draw_row,
    .select_click = menu_select,
  });
  menu_layer_set_click_config_onto_window(s_menu_layer, w);
#if defined(PBL_COLOR)
  menu_layer_set_highlight_colors(s_menu_layer, GColorRed, GColorWhite);
#endif
  layer_add_child(root, menu_layer_get_layer(s_menu_layer));
}

static void main_window_unload(Window *w) {
  menu_layer_destroy(s_menu_layer);
  s_menu_layer = NULL;
}

static void init(void) {
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
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
