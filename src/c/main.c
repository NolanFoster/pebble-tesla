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
#define KEY_PAINT_COLOR  MESSAGE_KEY_PAINT_COLOR
#define KEY_CHARGING     MESSAGE_KEY_CHARGING
#define KEY_CHARGE_LIMIT MESSAGE_KEY_CHARGE_LIMIT
#define KEY_CHARGE_TIME  MESSAGE_KEY_CHARGE_TIME
#define KEY_DIST_UNIT    MESSAGE_KEY_DIST_UNIT
#define KEY_SHOW_CLOCK   MESSAGE_KEY_SHOW_CLOCK

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
static int      s_charging    = CHARGE_UNKNOWN;  // ChargeState
static int      s_charge_limit = -1;             // target percent, <0 unknown
static int      s_charge_eta  = -1;              // minutes to limit while charging
static bool     s_dist_km     = false;           // render range in km (else miles)
static bool     s_show_clock  = true;            // show the current time atop the card
static bool     s_connected   = true;            // phone (PebbleKit JS) reachable
static char     s_name[100]   = "";   // vehicle name (UTF-8; room for ~24 emoji)
static char     s_error[64]   = "";
static int      s_paint       = PAINT_UNKNOWN;  // exterior paint -> accent theme
static bool     s_dark_fg     = false;          // accent is light -> draw fg in black

// ---- UI ----
static Window         *s_main_window;
static Layer          *s_card_layer;     // custom-drawn status "card" (left of action bar)
static Layer          *s_action_bar;     // lock (up) / settings (select) / climate (down)
// Bitmaps + accent currently shown on the custom action bar; the update_proc
// reads these. update_action_bar_icons()/apply_theme() set them then mark dirty.
static GBitmap        *s_ab_up, *s_ab_select, *s_ab_down;
static GColor          s_ab_accent;      // accent bg; set by apply_theme() before the first paint
static GBitmap        *s_icon_locked, *s_icon_unlocked, *s_icon_settings;
static GBitmap        *s_icon_climate_on, *s_icon_climate_off;
// Dark (black-glyph) variants of the action-bar icons, used on light accents
// (e.g. a white- or silver-car theme) so the glyphs stay visible.
static GBitmap        *s_icon_locked_d, *s_icon_unlocked_d, *s_icon_settings_d;
static GBitmap        *s_icon_climate_on_d, *s_icon_climate_off_d;
// "More Controls" menu glyphs, plus black-glyph variants for the highlighted
// row when the car theme makes that row's background light.
static GBitmap        *s_icon_temp_up, *s_icon_temp_down, *s_icon_frunk;
static GBitmap        *s_icon_trunk, *s_icon_charge, *s_icon_refresh;
static GBitmap        *s_icon_temp_up_d, *s_icon_temp_down_d, *s_icon_frunk_d;
static GBitmap        *s_icon_trunk_d, *s_icon_charge_d, *s_icon_refresh_d;

static Window      *s_controls_window;   // "More Controls" sub-window (select button)
static MenuLayer   *s_controls_menu;

static Window      *s_status_window;     // transient "Sending…" / result overlay
static TextLayer   *s_status_text;
static char         s_status_buf[64];
static AppTimer    *s_status_timer;

// Battery-fill animation: the gauge draws from s_anim_pct, which eases from the
// previous value to s_battery whenever a fresh reading arrives.
static int          s_anim_pct  = -1;    // value the gauge currently renders (-1 = none yet)
static int          s_anim_from = 0;
static int          s_anim_to   = 0;

// Forward decls
static void send_command(TeslaCommand cmd, int arg);
static void update_action_bar_icons(void);
static void push_controls_window(void);
static void apply_theme(void);
static void update_clock_subscription(void);

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
  text_layer_set_font(s_status_text, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
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
    .charging = s_charging, .charge_limit = s_charge_limit,
    .charge_eta = s_charge_eta, .dist_km = s_dist_km,
  };
}

// ---------------------------------------------------------------------------
// Persistent state cache
//
// The design guidelines recommend caching the last-loaded data so the card shows
// known values immediately on launch (and during a Bluetooth/phone outage)
// instead of dashes until the first round-trip completes. We snapshot the cached
// globals into one versioned struct via persist_write_data; a version bump
// invalidates an old layout rather than reading it back wrong.
// ---------------------------------------------------------------------------
#define PERSIST_KEY_STATE   1
#define PERSIST_VERSION      4   // bump when the struct layout changes

typedef struct {
  int32_t version;
  int32_t battery, range, inside_temp, target_temp, awake, paint;
  int32_t charging, charge_limit, charge_eta;
  uint8_t locked, climate_on, online, dist_km, show_clock;
  char    name[100];
} PersistState;

static void save_state(void) {
  PersistState p = {
    .version = PERSIST_VERSION,
    .battery = s_battery, .range = s_range,
    .inside_temp = s_inside_temp, .target_temp = s_target_temp,
    .awake = s_awake, .paint = s_paint,
    .charging = s_charging, .charge_limit = s_charge_limit, .charge_eta = s_charge_eta,
    .locked = s_locked, .climate_on = s_climate_on, .online = s_online,
    .dist_km = s_dist_km, .show_clock = s_show_clock,
  };
  strncpy(p.name, s_name, sizeof(p.name) - 1);
  p.name[sizeof(p.name) - 1] = '\0';
  persist_write_data(PERSIST_KEY_STATE, &p, sizeof(p));
}

// Populate the cached globals from the last saved snapshot (if any), so the
// first paint shows real values. Returns whether a usable cache was loaded.
static bool load_state(void) {
  if (!persist_exists(PERSIST_KEY_STATE)) return false;
  PersistState p;
  int read = persist_read_data(PERSIST_KEY_STATE, &p, sizeof(p));
  if (read != (int)sizeof(p) || p.version != PERSIST_VERSION) return false;
  s_battery = p.battery; s_range = p.range;
  s_inside_temp = p.inside_temp; s_target_temp = p.target_temp;
  s_awake = p.awake; s_paint = p.paint;
  s_charging = p.charging; s_charge_limit = p.charge_limit; s_charge_eta = p.charge_eta;
  s_locked = p.locked; s_climate_on = p.climate_on; s_online = p.online;
  s_dist_km = p.dist_km; s_show_clock = p.show_clock;
  strncpy(s_name, p.name, sizeof(s_name) - 1);
  s_name[sizeof(s_name) - 1] = '\0';
  s_anim_pct = s_battery;   // snap the gauge to the cached level (no sweep-up)
  return true;
}

// ---------------------------------------------------------------------------
// Battery-fill animation
// ---------------------------------------------------------------------------
static void batt_anim_update(Animation *a, const AnimationProgress p) {
  s_anim_pct = s_anim_from + (s_anim_to - s_anim_from) * p / ANIMATION_NORMALIZED_MAX;
  if (s_card_layer) layer_mark_dirty(s_card_layer);
}

static const AnimationImplementation s_batt_anim_impl = {
  .update = batt_anim_update,
};

// Ease the gauge from its current value to `to`. Snaps (no animation) on the
// very first known reading so the card doesn't sweep up from 0 on launch. The
// scheduled animation is owned and auto-destroyed by the framework; we never
// retain the pointer, and cancel any in-flight sweep with unschedule_all.
static void animate_battery_to(int to) {
  animation_unschedule_all();
  if (s_anim_pct < 0 || to < 0) {        // first/unknown reading: just snap
    s_anim_pct = to;
    if (s_card_layer) layer_mark_dirty(s_card_layer);
    return;
  }
  s_anim_from = s_anim_pct;
  s_anim_to   = to;
  Animation *a = animation_create();
  animation_set_implementation(a, &s_batt_anim_impl);
  animation_set_duration(a, 600);
  animation_set_curve(a, AnimationCurveEaseOut);
  animation_schedule(a);
}

// ---------------------------------------------------------------------------
// Status card (left of the action bar)
//
// Visual language: Frank Lloyd Wright / Usonian. A warm earthy palette on a
// black ground, a large geometric (LECO) battery "hero" ring, and an art-glass
// ornament band of stepped squares separating the hero from the status rows.
// ---------------------------------------------------------------------------

// Cherokee-Red signature accent — the single action color (action bar, menu
// highlight). On 1-bit displays there is no accent (handled at call sites).
#define ACCENT_COLOR PBL_IF_COLOR_ELSE(GColorRoseVale, GColorWhite)

// Custom action bar geometry. Wider than the stock ACTION_BAR_WIDTH (~30 rect /
// ~20 round) so the larger glyphs have breathing room. The top/down glyphs sit
// AB_ICON_EDGE_PAD from the screen edge; on round we keep a bigger pad so the
// corner icons stay inside the visible disc.
#define AB_WIDTH          PBL_IF_ROUND_ELSE(34, 36)
#define AB_ICON_EDGE_PAD  PBL_IF_ROUND_ELSE(22, 6)

#if defined(PBL_COLOR)
// Battery arc: vibrant red -> yellow -> green, the most saturated colors in the
// Pebble palette so the charge level pops against the black ground and the dark
// track.
static GColor battery_arc_color(int battery) {
  switch (battery_level(battery)) {
    case BATTERY_LOW:  return GColorRed;        // critical
    case BATTERY_MED:  return GColorYellow;     // mid
    case BATTERY_HIGH: return GColorGreen;      // healthy
    default:           return GColorLightGray;  // unknown — neutral, still legible
  }
}
#endif

// Circular battery gauge: a ring whose filled arc tracks the charge level, with
// the charge percentage as a big geometric (LECO) number centered inside. The
// arc sweeps clockwise from 12 o'clock (graphics_fill_radial's 0 angle). `pct`
// is passed in (rather than read from st) so the fill animation can drive it;
// the *known/unknown* distinction still comes from st->battery.
static void draw_battery_gauge(GContext *gctx, GRect box, const VehicleState *st, int pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  const uint16_t thick = PBL_IF_ROUND_ELSE(8, 7);
  const int32_t end = TRIG_MAX_ANGLE * pct / 100;
  // Charge-limit angle (a notch on the ring), used to draw a "pending" arc from
  // the current level to the limit while charging and a tick at the target.
  const int32_t lim_end = (st->charge_limit > 0 && st->charge_limit <= 100)
    ? TRIG_MAX_ANGLE * st->charge_limit / 100 : -1;

#if defined(PBL_COLOR)
  const bool charging = (st->charging == CHARGE_CHARGING);
  graphics_context_set_fill_color(gctx, GColorDarkGray);                  // track
  graphics_fill_radial(gctx, box, GOvalScaleModeFitCircle, thick, 0, TRIG_MAX_ANGLE);
  if (charging && lim_end > end) {                                        // pending → limit
    graphics_context_set_fill_color(gctx, GColorMintGreen);
    graphics_fill_radial(gctx, box, GOvalScaleModeFitCircle, thick, end, lim_end);
  }
  graphics_context_set_fill_color(gctx, battery_arc_color(st->battery));  // charge
  graphics_fill_radial(gctx, box, GOvalScaleModeFitCircle, thick, 0, end);
#else
  // 1-bit: a thin full track ring plus a thicker progress arc (the extra
  // thickness, not color, conveys the level).
  graphics_context_set_fill_color(gctx, GColorBlack);
  graphics_fill_radial(gctx, box, GOvalScaleModeFitCircle, 2, 0, TRIG_MAX_ANGLE);
  graphics_fill_radial(gctx, box, GOvalScaleModeFitCircle, thick, 0, end);
#endif

  // Charge-limit tick: a short notch poking inward at the target angle, so the
  // limit is legible whether or not the car is currently charging. Skipped at
  // 100% (it would sit on the 12-o'clock start) and when the limit is unknown.
  if (lim_end >= 0 && st->charge_limit < 100) {
    const int32_t tickw = TRIG_MAX_ANGLE / 90;        // ~2° wide
    int32_t a0 = lim_end - tickw; if (a0 < 0) a0 = 0;
    int32_t a1 = lim_end + tickw; if (a1 > TRIG_MAX_ANGLE) a1 = TRIG_MAX_ANGLE;
    graphics_context_set_fill_color(gctx, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
    graphics_fill_radial(gctx, box, GOvalScaleModeFitCircle, thick + 3, a0, a1);
  }

  // Percentage hero, centered. The LECO numbers font scales with the ring so it
  // never collides with the arc; an unknown reading uses a GOTHIC em-dash (LECO
  // has no dash glyph).
  graphics_context_set_text_color(gctx, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
  int16_t num_h;
  GFont f_num;
  if (st->battery < 0) {
    f_num = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
    num_h = 30;
  } else if (box.size.w >= 92) {
    f_num = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
    num_h = 42;
  } else if (box.size.w >= 68) {
    f_num = fonts_get_system_font(FONT_KEY_LECO_38_BOLD_NUMBERS);
    num_h = 38;
  } else {
    f_num = fonts_get_system_font(FONT_KEY_LECO_28_LIGHT_NUMBERS);
    num_h = 28;
  }
  char buf[16];
  fmt_battery_num(st, buf, sizeof(buf));
  graphics_draw_text(gctx, buf, f_num,
    GRect(box.origin.x, box.origin.y + (box.size.h - num_h) / 2 - 4, box.size.w, num_h),
    GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// Art-glass ornament: a thin "prairie rule" with three ascending stepped
// squares centered on it — Wright's Tree-of-Life motif abstracted into a few
// primitives. Marks the compression point between the hero and the status rows.
// Rectangular displays only (round has no room and never calls it).
#if !defined(PBL_ROUND)
static void draw_ornament(GContext *gctx, int16_t cx, int16_t y, int16_t w) {
  const GColor c = PBL_IF_COLOR_ELSE(GColorBrass, GColorBlack);
  graphics_context_set_fill_color(gctx, c);
  graphics_fill_rect(gctx, GRect(cx - w / 2, y, w, 1), 0, GCornerNone);
  const int16_t sz[3] = {3, 4, 5};
  const int16_t gap = 4;
  const int16_t total = sz[0] + sz[1] + sz[2] + 2 * gap;
  int16_t x = cx - total / 2;
  for (int i = 0; i < 3; i++) {
    graphics_fill_rect(gctx, GRect(x, y - sz[i], sz[i], sz[i]), 0, GCornerNone);
    x += sz[i] + gap;
  }
}
#endif

// One status line: a leading state-colored dot plus its label. On round the
// centered layout has no room for a left dot, so the text itself is tinted; on
// 1-bit the dot is a filled (true) or hollow (false) circle.
static void draw_status_row(GContext *gctx, GRect content, int16_t y, int16_t h,
                            GColor accent, bool filled, const char *text, GFont f) {
#if defined(PBL_ROUND)
  graphics_context_set_text_color(gctx, accent);
  graphics_draw_text(gctx, text, f, GRect(content.origin.x, y, content.size.w, h),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  (void)filled;
#else
  const int16_t r = 4;
  const GPoint dot = GPoint(content.origin.x + r, y + h / 2);
#if defined(PBL_COLOR)
  graphics_context_set_fill_color(gctx, accent);
  graphics_fill_circle(gctx, dot, r);
  graphics_context_set_text_color(gctx, GColorWhite);
#else
  if (filled) {
    graphics_context_set_fill_color(gctx, GColorBlack);
    graphics_fill_circle(gctx, dot, r);
  } else {
    graphics_context_set_stroke_color(gctx, GColorBlack);
    graphics_draw_circle(gctx, dot, r);
  }
  graphics_context_set_text_color(gctx, GColorBlack);
  (void)accent;
#endif
  graphics_draw_text(gctx, text, f,
    GRect(content.origin.x + 2 * r + 6, y, content.size.w - 2 * r - 6, h),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
#endif
}

// Draws a single screenful of vehicle status: name, the battery hero, an
// art-glass ornament band, then lock / climate / power rows. Strings come from
// the shared logic.c formatters so the card text stays consistent with the rest
// of the app. Layout is bounds-relative so taller screens (emery) breathe.
static void card_update_proc(Layer *layer, GContext *gctx) {
  GRect b = layer_get_bounds(layer);
  VehicleState st = current_state();

  GColor fg = PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack);
  const GTextAlignment align = PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft);
  const int16_t pad = PBL_IF_ROUND_ELSE(0, 6);
  GRect content = GRect(b.origin.x + pad, b.origin.y,
                        b.size.w - 2 * pad, b.size.h);

  char line[40];
  GFont f_name = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GFont f_body = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

  // Slot heights for the fixed elements; the battery hero flexes to fill what's
  // left so short screens (basalt/diorite, 168px) stay tight while emery (228px)
  // gives the hero a commanding diameter. `bot` keeps the lowest row inside the
  // circle on round. Round wastes its corners, so it drops the power footer and
  // shows two status rows; rectangular displays show three.
  const int16_t y0  = PBL_IF_ROUND_ELSE(20, 6);
  const int16_t bot = PBL_IF_ROUND_ELSE(20, 4);
  const int16_t avail = content.size.h - bot;                // usable bottom edge offset
  const int16_t name_slot = 28, range_slot = 18;
  // A glanceable clock atop the card (optional). When shown it claims its own
  // slot; the battery hero flexes below to absorb it where there's slack (emery).
  // On the short 168px screens the hero is already pinned to its floor, so we
  // also drop the decorative ornament band (below) to fund the clock's row
  // rather than push the footer off-screen.
  const int16_t clock_slot = s_show_clock ? 20 : 0;
  const int16_t orn_slot = PBL_IF_ROUND_ELSE(0, s_show_clock ? 0 : 8);  // no ornament on the cramped circle
  const int16_t rows_reserve = PBL_IF_ROUND_ELSE(34, 56);
  const int      n_rows = PBL_IF_ROUND_ELSE(2, 3);
  int16_t diam = avail - y0 - clock_slot - name_slot - range_slot - orn_slot - rows_reserve;
  const int16_t dmax = PBL_IF_ROUND_ELSE(96, 110);
  if (diam > content.size.w - 8) diam = content.size.w - 8;  // never overflow narrow cards
  if (diam > dmax) diam = dmax;
  if (diam < 54) diam = 54;                                  // keep room for the LECO number

  int16_t y = y0;

  // Current time, so the clock stays glanceable while the app is left open.
  // Read straight from the watch (no phone needed); a MINUTE_UNIT tick redraws
  // the card. Follows the watch's 12h/24h system setting.
  if (s_show_clock) {
    char tbuf[8];
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    bool h24 = clock_is_24h_style();
    strftime(tbuf, sizeof(tbuf), h24 ? "%H:%M" : "%I:%M", lt);
    char *tstr = tbuf;
    if (!h24 && tbuf[0] == '0') tstr++;  // "09:41" -> "9:41"
    graphics_context_set_text_color(gctx, fg);
    graphics_draw_text(gctx, tstr, f_body,
      GRect(content.origin.x, y, content.size.w, clock_slot),
      GTextOverflowModeTrailingEllipsis, align, NULL);
    y += clock_slot;
  }

  // Vehicle name (or "Status").
  graphics_context_set_text_color(gctx, fg);
  graphics_draw_text(gctx, status_header_text(s_name), f_name,
    GRect(content.origin.x, y, content.size.w, 28),
    GTextOverflowModeTrailingEllipsis, align, NULL);
  y += name_slot;

  // Battery hero: a bold ring, centered in the card width.
  GRect gauge = GRect(content.origin.x + (content.size.w - diam) / 2, y, diam, diam);
  draw_battery_gauge(gctx, gauge, &st, s_anim_pct);
  y += diam + 2;

  // Range, just beneath the ring.
  graphics_context_set_text_color(gctx, fg);
  fmt_range(&st, line, sizeof(line));
  graphics_draw_text(gctx, line, f_body,
    GRect(content.origin.x, y, content.size.w, range_slot),
    GTextOverflowModeTrailingEllipsis, align, NULL);
  y += range_slot;

  // Art-glass ornament band: the compression point before the status rows.
  // Skipped on round (no room) and when the clock is shown (its row reclaims
  // the band's space on the short screens).
#if !defined(PBL_ROUND)
  if (!s_show_clock) draw_ornament(gctx, content.origin.x + content.size.w / 2, y + 5, content.size.w);
#endif
  y += orn_slot;

  // Status rows distribute evenly through the remaining height (down to the
  // bottom safe edge) so taller screens space them out instead of clustering.
  const int16_t rows_h = content.origin.y + avail - y;
  const int16_t pitch = rows_h / n_rows;
  const int16_t row_h = pitch < 22 ? pitch : 22;

  // Doors (lock state): green when secured, ochre when open. The color names
  // live only in the color arm so 1-bit builds (where they're undefined and
  // the dot is drawn black) still compile.
  fmt_lock_subtitle(&st, line, sizeof(line));
  draw_status_row(gctx, content, y, row_h,
    PBL_IF_COLOR_ELSE(st.locked ? GColorArmyGreen : GColorChromeYellow, GColorBlack),
    st.locked, line, f_body);
  y += pitch;

  // Climate (may be longer: "On  •  in 21° → 22°"): warm orange when running.
  fmt_climate_subtitle(&st, line, sizeof(line));
  draw_status_row(gctx, content, y, row_h,
    PBL_IF_COLOR_ELSE(st.climate_on ? GColorOrange : GColorWindsorTan, GColorBlack),
    st.climate_on, line, f_body);
  y += pitch;

  // Footer (small) — rectangular displays only; round has no room. Priority:
  // a lost phone link first (everything else is stale without it), then the
  // charge status while charging/done, otherwise the awake/power state.
#if !defined(PBL_ROUND)
  if (!s_connected) {
    snprintf(line, sizeof(line), "Phone offline");
    graphics_context_set_text_color(gctx, PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorBlack));
  } else if (charge_show_status(&st)) {
    fmt_charge_subtitle(&st, line, sizeof(line));
    graphics_context_set_text_color(gctx,
      PBL_IF_COLOR_ELSE(st.charging == CHARGE_CHARGING ? GColorGreen : fg, GColorBlack));
  } else {
    fmt_power_subtitle(&st, line, sizeof(line));
    graphics_context_set_text_color(gctx, fg);
  }
  graphics_draw_text(gctx, line, fonts_get_system_font(FONT_KEY_GOTHIC_14),
    GRect(content.origin.x, y, content.size.w, 18),
    GTextOverflowModeTrailingEllipsis, align, NULL);
#endif
}

// ---------------------------------------------------------------------------
// Action bar (lock = up, settings = select, climate = down)
// ---------------------------------------------------------------------------
// Draw one centered icon at vertical position y within the bar bounds.
static void ab_draw_icon(GContext *ctx, GRect b, GBitmap *icon, int16_t y) {
  if (!icon) return;
  GRect r = gbitmap_get_bounds(icon);
  int16_t x = b.origin.x + (b.size.w - r.size.w) / 2;
  graphics_draw_bitmap_in_rect(ctx, icon, GRect(x, y, r.size.w, r.size.h));
}

// Custom action bar: fill with the accent color, then draw the up/select/down
// glyphs ourselves so we control the column width and pin the up/down icons to
// the screen edges (the stock ActionBarLayer allowed neither).
static void action_bar_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, s_ab_accent);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  // Honor the transparent PNG alpha so clear pixels show the accent fill.
  graphics_context_set_compositing_mode(ctx, PBL_IF_COLOR_ELSE(GCompOpSet, GCompOpAssign));

  ab_draw_icon(ctx, b, s_ab_up, b.origin.y + AB_ICON_EDGE_PAD);
  if (s_ab_select) {
    GRect r = gbitmap_get_bounds(s_ab_select);
    ab_draw_icon(ctx, b, s_ab_select, b.origin.y + (b.size.h - r.size.h) / 2);
  }
  if (s_ab_down) {
    GRect r = gbitmap_get_bounds(s_ab_down);
    ab_draw_icon(ctx, b, s_ab_down,
                 b.origin.y + b.size.h - r.size.h - AB_ICON_EDGE_PAD);
  }
}

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

// Refresh the up/down glyphs to mirror the current lock/climate state. On a
// light accent (s_dark_fg) the black-glyph variants are used so the icons keep
// contrast against the action bar.
static void update_action_bar_icons(void) {
  if (!s_action_bar) return;
  VehicleState st = current_state();
  bool dark = PBL_IF_COLOR_ELSE(s_dark_fg, false);
  GBitmap *i_locked     = dark ? s_icon_locked_d     : s_icon_locked;
  GBitmap *i_unlocked   = dark ? s_icon_unlocked_d   : s_icon_unlocked;
  GBitmap *i_settings   = dark ? s_icon_settings_d   : s_icon_settings;
  GBitmap *i_climate_on = dark ? s_icon_climate_on_d : s_icon_climate_on;
  GBitmap *i_climate_off= dark ? s_icon_climate_off_d: s_icon_climate_off;
  GBitmap *up = (lock_toggle_icon(&st) == ICON_KIND_LOCKED) ? i_locked
                                                            : i_unlocked;
  GBitmap *down = (climate_toggle_icon(&st) == ICON_KIND_CLIMATE_ON) ? i_climate_on
                                                                     : i_climate_off;
  s_ab_up     = up;
  s_ab_select = i_settings;
  s_ab_down   = down;
  layer_mark_dirty(s_action_bar);
}

// ---------------------------------------------------------------------------
// Car-color theme: map the reported paint onto an accent color plus a
// contrast-safe foreground. The accent is used as a *background* (action bar,
// menu highlight) behind icons/text, so light accents (white, silver) pair with
// a black foreground and dark accents with white — keeping everything legible.
// ---------------------------------------------------------------------------
#if defined(PBL_COLOR)
typedef struct { GColor accent; bool dark_fg; } Theme;

static Theme theme_for_paint(int paint) {
  switch (paint) {
    case PAINT_WHITE:  return (Theme){ GColorWhite,     true  };
    case PAINT_BLACK:  return (Theme){ GColorBlack,     false };
    case PAINT_SILVER: return (Theme){ GColorLightGray, true  };
    case PAINT_GREY:   return (Theme){ GColorDarkGray,  false };
    case PAINT_BLUE:   return (Theme){ GColorDukeBlue,  false };
    case PAINT_RED:    // brand red is the Usonian Cherokee-Red signature
    default:           return (Theme){ ACCENT_COLOR,    false }; // unknown -> brand accent
  }
}

static GColor theme_fg(const Theme *t) {
  return t->dark_fg ? GColorBlack : GColorWhite;
}

static void apply_theme(void) {
  Theme th = theme_for_paint(s_paint);
  s_dark_fg = th.dark_fg;
  s_ab_accent = th.accent;
  if (s_action_bar) layer_mark_dirty(s_action_bar);
  update_action_bar_icons();  // picks the glyph polarity that matches s_dark_fg
  if (s_controls_menu)
    menu_layer_set_highlight_colors(s_controls_menu, th.accent, theme_fg(&th));
}
#else
// Monochrome platforms keep their fixed black/white scheme.
static void apply_theme(void) { s_ab_accent = GColorWhite; update_action_bar_icons(); }
#endif

// ---------------------------------------------------------------------------
// "More Controls" sub-window (opened from the SELECT button)
// ---------------------------------------------------------------------------
// The visible rows depend on state — the Wake row only appears when the car is
// asleep — so rebuild the list whenever a callback needs it. The menu reloads
// on every state update (see inbox_received), so Wake appears/disappears live.
static int mc_rows(ControlsRow *rows) {
  VehicleState st = current_state();
  return controls_menu_rows(&st, rows, CONTROLS_MAX_ROWS);
}

static uint16_t mc_num_rows(MenuLayer *ml, uint16_t section, void *ctx) {
  ControlsRow rows[CONTROLS_MAX_ROWS];
  return (uint16_t)mc_rows(rows);
}

static int16_t mc_header_height(MenuLayer *ml, uint16_t section, void *ctx) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void mc_draw_header(GContext *gctx, const Layer *cell, uint16_t section, void *ctx) {
  menu_cell_basic_header_draw(gctx, cell, "More Controls");
#if defined(PBL_COLOR)
  // Brass "prairie rule" under the header to echo the card ornament.
  GRect cb = layer_get_bounds((Layer *)cell);
  graphics_context_set_fill_color(gctx, GColorBrass);
  graphics_fill_rect(gctx, GRect(cb.origin.x, cb.origin.y + cb.size.h - 2,
                                 cb.size.w, 2), 0, GCornerNone);
#endif
}

// Per-row glyph (black variant when `dark`, to stay legible on a light accent).
// The Wake row has no dedicated glyph, so it draws icon-less.
static GBitmap *mc_icon(ControlsRow row, bool dark) {
  switch (row) {
    case CTRL_ROW_WAKE:        return NULL;
    case CTRL_ROW_TEMP_UP:     return dark ? s_icon_temp_up_d   : s_icon_temp_up;
    case CTRL_ROW_TEMP_DOWN:   return dark ? s_icon_temp_down_d : s_icon_temp_down;
    case CTRL_ROW_FRUNK:       return dark ? s_icon_frunk_d     : s_icon_frunk;
    case CTRL_ROW_TRUNK:       return dark ? s_icon_trunk_d     : s_icon_trunk;
    case CTRL_ROW_CHARGE_PORT: return dark ? s_icon_charge_d    : s_icon_charge;
    case CTRL_ROW_CHARGE:      return dark ? s_icon_charge_d    : s_icon_charge;
    case CTRL_ROW_LIMIT_UP:    return NULL;
    case CTRL_ROW_LIMIT_DOWN:  return NULL;
    case CTRL_ROW_REFRESH:     return dark ? s_icon_refresh_d   : s_icon_refresh;
  }
  return NULL;
}

static void mc_draw_row(GContext *gctx, const Layer *cell, MenuIndex *idx, void *ctx) {
  // The highlighted row paints over the car-color accent. On a light accent
  // (s_dark_fg) its white glyph would vanish, so use the black variant for just
  // that row; non-highlighted rows keep white glyphs on the dark menu. The
  // title text already follows the menu's highlight foreground.
  bool dark = false;
#if defined(PBL_COLOR)
  if (s_dark_fg) {
    MenuIndex sel = menu_layer_get_selected_index(s_controls_menu);
    dark = (sel.section == idx->section && sel.row == idx->row);
  }
#endif
  ControlsRow rows[CONTROLS_MAX_ROWS];
  int n = mc_rows(rows);
  if (idx->row >= n) { menu_cell_basic_draw(gctx, cell, "", NULL, NULL); return; }
  ControlsRow row = rows[idx->row];
  // The charge row is a state-aware toggle, so its label flips Start/Stop.
  VehicleState st = current_state();
  const char *label = (row == CTRL_ROW_CHARGE) ? charge_toggle_label(&st)
                                               : controls_row_label(row);
  menu_cell_basic_draw(gctx, cell, label, NULL, mc_icon(row, dark));
}

// Transient "…ing" overlay shown after a row is selected.
static const char *mc_status(ControlsRow row) {
  switch (row) {
    case CTRL_ROW_WAKE:        return "Waking…";
    case CTRL_ROW_TEMP_UP:     return "Temp +1°…";
    case CTRL_ROW_TEMP_DOWN:   return "Temp -1°…";
    case CTRL_ROW_FRUNK:       return "Opening frunk…";
    case CTRL_ROW_TRUNK:       return "Opening trunk…";
    case CTRL_ROW_CHARGE_PORT: return "Charge port…";
    case CTRL_ROW_CHARGE:      return "Charging…";   // dynamic; see mc_select
    case CTRL_ROW_LIMIT_UP:    return "Limit +5%…";
    case CTRL_ROW_LIMIT_DOWN:  return "Limit -5%…";
    case CTRL_ROW_REFRESH:     return "Refreshing…";
  }
  return "…";
}

static void mc_select(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  ControlsRow rows[CONTROLS_MAX_ROWS];
  int n = mc_rows(rows);
  if (idx->row >= n) return;
  ControlsRow row = rows[idx->row];
  VehicleState st = current_state();
  // The charge row toggles Start/Stop based on the current charging state.
  if (row == CTRL_ROW_CHARGE) {
    send_command((TeslaCommand)charge_toggle_cmd(&st), 0);
    show_status(st.charging == CHARGE_CHARGING ? "Stopping…" : "Starting…", 0);
    return;
  }
  // send_command attaches TEMP_DELTA only for temp rows; a 0 delta is ignored.
  send_command((TeslaCommand)controls_row_cmd(row), controls_row_temp_delta(row));
  show_status(mc_status(row), 0);
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
  // Dark, cohesive theme so the white glyphs read on every row and the menu
  // matches the card. The accent highlight is the Cherokee-Red signature.
  menu_layer_set_normal_colors(s_controls_menu, GColorBlack, GColorWhite);
#if defined(PBL_COLOR)
  // Highlight follows the car-color theme; the row icons flip polarity to match
  // (see mc_draw_row) so they stay visible on light accents.
  Theme th = theme_for_paint(s_paint);
  menu_layer_set_highlight_colors(s_controls_menu, th.accent, theme_fg(&th));
#else
  menu_layer_set_highlight_colors(s_controls_menu, GColorWhite, GColorBlack);
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
  // The card footer flags a lost phone link (see connection_handler), but we no
  // longer hard-block the send on s_connected: connection_service_peek can read
  // "disconnected" while AppMessage actually still works, which would wrongly
  // reject valid commands. Always attempt the send; if the link really is down,
  // outbox_failed surfaces "Send failed".
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
    vibes_long_pulse();                 // a longer buzz flags something needing attention
    return;
  }
  if ((t = dict_find(it, KEY_STATUS))) {
    // A short human string confirming a command landed. A confirmation (not an
    // in-progress "…ing" message) earns a short success buzz.
    show_status(t->value->cstring, 1500);
    if (status_is_confirmation(t->value->cstring)) vibes_short_pulse();
  }
  if ((t = dict_find(it, KEY_BATTERY)))     { s_battery = t->value->int32; got_state = true;
                                              animate_battery_to(s_battery); }
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
  if ((t = dict_find(it, KEY_PAINT_COLOR))) {
    int paint = t->value->int32;
    if (paint != s_paint) { s_paint = paint; apply_theme(); }
    got_state = true;
  }
  if ((t = dict_find(it, KEY_CHARGING)))     { s_charging = t->value->int32; got_state = true; }
  if ((t = dict_find(it, KEY_CHARGE_LIMIT))) { s_charge_limit = t->value->int32; got_state = true; }
  if ((t = dict_find(it, KEY_CHARGE_TIME)))  { s_charge_eta = t->value->int32; got_state = true; }
  if ((t = dict_find(it, KEY_DIST_UNIT)))    { s_dist_km = t->value->int32 != 0; got_state = true; }
  if ((t = dict_find(it, KEY_SHOW_CLOCK))) {
    bool v = t->value->int32 != 0;
    if (v != s_show_clock) { s_show_clock = v; update_clock_subscription(); }
    got_state = true;
  }

  if (got_state) {
    save_state();                                     // cache for the next cold launch
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
  vibes_long_pulse();
}

static void outbox_failed(DictionaryIterator *it, AppMessageResult reason, void *ctx) {
  show_status("Send failed", 1500);
  vibes_long_pulse();
}

static void outbox_sent(DictionaryIterator *it, void *ctx) {
  // command reached the phone; await STATUS/state reply
}

// ---------------------------------------------------------------------------
// Bluetooth / phone connection
//
// The design guidelines call for handling a lost link gracefully. We track the
// PebbleKit-JS (phone app) connection, surface a "Phone offline" footer, and
// short-circuit button presses with the same message (see send_command) so a tap
// gets instant feedback instead of a delayed "Send failed".
// ---------------------------------------------------------------------------
static void connection_handler(bool connected) {
  s_connected = connected;
  if (s_card_layer) layer_mark_dirty(s_card_layer);
}

// ---------------------------------------------------------------------------
// Clock tick
//
// The card reads the watch clock at draw time; this just nudges a redraw each
// minute so the displayed time stays current while the app is left open. We
// only subscribe while the clock is actually shown, to avoid waking the app
// every minute when the option is off.
// ---------------------------------------------------------------------------
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  if (s_card_layer) layer_mark_dirty(s_card_layer);
}

static void update_clock_subscription(void) {
  if (s_show_clock) {
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  } else {
    tick_timer_service_unsubscribe();
  }
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
  s_icon_locked_d      = gbitmap_create_with_resource(RESOURCE_ID_ICON_LOCKED_DARK);
  s_icon_unlocked_d    = gbitmap_create_with_resource(RESOURCE_ID_ICON_UNLOCKED_DARK);
  s_icon_settings_d    = gbitmap_create_with_resource(RESOURCE_ID_ICON_SETTINGS_DARK);
  s_icon_climate_on_d  = gbitmap_create_with_resource(RESOURCE_ID_ICON_CLIMATE_ON_DARK);
  s_icon_climate_off_d = gbitmap_create_with_resource(RESOURCE_ID_ICON_CLIMATE_OFF_DARK);
  s_icon_temp_up     = gbitmap_create_with_resource(RESOURCE_ID_ICON_TEMP_UP);
  s_icon_temp_down   = gbitmap_create_with_resource(RESOURCE_ID_ICON_TEMP_DOWN);
  s_icon_frunk       = gbitmap_create_with_resource(RESOURCE_ID_ICON_FRUNK);
  s_icon_trunk       = gbitmap_create_with_resource(RESOURCE_ID_ICON_TRUNK);
  s_icon_charge      = gbitmap_create_with_resource(RESOURCE_ID_ICON_CHARGE);
  s_icon_refresh     = gbitmap_create_with_resource(RESOURCE_ID_ICON_REFRESH);
  s_icon_temp_up_d   = gbitmap_create_with_resource(RESOURCE_ID_ICON_TEMP_UP_DARK);
  s_icon_temp_down_d = gbitmap_create_with_resource(RESOURCE_ID_ICON_TEMP_DOWN_DARK);
  s_icon_frunk_d     = gbitmap_create_with_resource(RESOURCE_ID_ICON_FRUNK_DARK);
  s_icon_trunk_d     = gbitmap_create_with_resource(RESOURCE_ID_ICON_TRUNK_DARK);
  s_icon_charge_d    = gbitmap_create_with_resource(RESOURCE_ID_ICON_CHARGE_DARK);
  s_icon_refresh_d   = gbitmap_create_with_resource(RESOURCE_ID_ICON_REFRESH_DARK);
}

static void unload_icons(void) {
  gbitmap_destroy(s_icon_locked);      s_icon_locked = NULL;
  gbitmap_destroy(s_icon_unlocked);    s_icon_unlocked = NULL;
  gbitmap_destroy(s_icon_settings);    s_icon_settings = NULL;
  gbitmap_destroy(s_icon_climate_on);  s_icon_climate_on = NULL;
  gbitmap_destroy(s_icon_climate_off); s_icon_climate_off = NULL;
  gbitmap_destroy(s_icon_locked_d);      s_icon_locked_d = NULL;
  gbitmap_destroy(s_icon_unlocked_d);    s_icon_unlocked_d = NULL;
  gbitmap_destroy(s_icon_settings_d);    s_icon_settings_d = NULL;
  gbitmap_destroy(s_icon_climate_on_d);  s_icon_climate_on_d = NULL;
  gbitmap_destroy(s_icon_climate_off_d); s_icon_climate_off_d = NULL;
  gbitmap_destroy(s_icon_temp_up);     s_icon_temp_up = NULL;
  gbitmap_destroy(s_icon_temp_down);   s_icon_temp_down = NULL;
  gbitmap_destroy(s_icon_frunk);       s_icon_frunk = NULL;
  gbitmap_destroy(s_icon_trunk);       s_icon_trunk = NULL;
  gbitmap_destroy(s_icon_charge);      s_icon_charge = NULL;
  gbitmap_destroy(s_icon_refresh);     s_icon_refresh = NULL;
  gbitmap_destroy(s_icon_temp_up_d);   s_icon_temp_up_d = NULL;
  gbitmap_destroy(s_icon_temp_down_d); s_icon_temp_down_d = NULL;
  gbitmap_destroy(s_icon_frunk_d);     s_icon_frunk_d = NULL;
  gbitmap_destroy(s_icon_trunk_d);     s_icon_trunk_d = NULL;
  gbitmap_destroy(s_icon_charge_d);    s_icon_charge_d = NULL;
  gbitmap_destroy(s_icon_refresh_d);   s_icon_refresh_d = NULL;
}

static void main_window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);

  window_set_background_color(w, PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite));

  // Status card fills everything except the action bar column (on rectangular
  // displays). On round the action bar overlaps the edge, so use full bounds.
  GRect card = PBL_IF_ROUND_ELSE(
    b,
    GRect(b.origin.x, b.origin.y, b.size.w - AB_WIDTH, b.size.h));
  s_card_layer = layer_create(card);
  layer_set_update_proc(s_card_layer, card_update_proc);
  layer_add_child(root, s_card_layer);

  // Custom action bar pinned to the right edge (replaces the stock ActionBarLayer
  // so we control width, icon size, and the top/bottom edge alignment).
  GRect ab = GRect(b.size.w - AB_WIDTH, b.origin.y, AB_WIDTH, b.size.h);
  s_action_bar = layer_create(ab);
  layer_set_update_proc(s_action_bar, action_bar_update_proc);
  layer_add_child(root, s_action_bar);
  window_set_click_config_provider(w, action_bar_click_config);
  apply_theme();  // accent bg + icon polarity for the current paint (defaults to ACCENT_COLOR)
}

static void main_window_unload(Window *w) {
  animation_unschedule_all();
  layer_destroy(s_action_bar);
  s_action_bar = NULL;
  layer_destroy(s_card_layer);
  s_card_layer = NULL;
}

static void init(void) {
  load_icons();
  load_state();   // show last-known values immediately, before the first refresh
  update_clock_subscription();  // start ticking if the cached setting wants the clock

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

  // Track the phone link so the card can flag a lost connection and button
  // presses fail fast (see connection_handler / send_command).
  s_connected = connection_service_peek_pebble_app_connection();
  connection_service_subscribe((ConnectionHandlers){
    .pebble_app_connection_handler = connection_handler,
  });

  window_stack_push(s_main_window, true);

  // Pull initial state once JS is ready (JS also auto-refreshes on 'ready')
  send_command(CMD_REFRESH, 0);
}

static void deinit(void) {
  connection_service_unsubscribe();
  if (s_controls_window) window_destroy(s_controls_window);
  window_destroy(s_main_window);
  unload_icons();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
