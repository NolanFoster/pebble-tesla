// PebbleKit JS — runs in the phone's Pebble app.
// Bridges the watch <-> Tessie API (https://api.tessie.com).
//
// Tessie endpoints used:
//   GET  https://api.tessie.com/{vin}/state
//   POST https://api.tessie.com/{vin}/command/{name}?wait_for_completion=true
// Auth: Authorization: Bearer <TESSIE_TOKEN>

// ---- Command codes (must match the enum in main.c) ----
var CMD = {
  REFRESH: 0, LOCK: 1, UNLOCK: 2,
  CLIMATE_ON: 3, CLIMATE_OFF: 4,
  TEMP_UP: 5, TEMP_DOWN: 6,
  FRUNK: 7, TRUNK: 8, CHARGE_PORT: 9,
  WAKE: 10,
  CHARGE_START: 11, CHARGE_STOP: 12,
  LIMIT_UP: 13, LIMIT_DOWN: 14
};

var API_BASE = 'https://api.tessie.com';

// Post-command state polling (see refreshAfterCommand).
var REFRESH_FIRST_MS = 800;   // wait before the first read after a command
var REFRESH_RETRY_MS = 1500;  // gap between subsequent reads while state lags
var REFRESH_MAX_READS = 4;    // give up after this many reads (~5s of polling)

// ---- Config persisted via localStorage (set by config page) ----
function getConfig() {
  return {
    token: localStorage.getItem('tessie_token') || '',
    vin: localStorage.getItem('tessie_vin') || '',
    useFahrenheit: localStorage.getItem('use_f') === '1',
    useKm: localStorage.getItem('use_km') === '1',
    showClock: localStorage.getItem('show_clock') !== '0', // default on (absent → shown)
    lastTarget: parseInt(localStorage.getItem('last_target') || '21', 10), // °C
    lastLimit: parseInt(localStorage.getItem('last_limit') || '80', 10)    // charge %
  };
}

function sendToWatch(dict) {
  Pebble.sendAppMessage(dict,
    function () {},
    function (e) { console.log('sendAppMessage failed: ' + JSON.stringify(e)); });
}

function sendError(msg) {
  sendToWatch({ ERROR: String(msg).substring(0, 60) });
}

// ---- HTTP helpers ----
function tessie(method, path, cb, timeoutMs) {
  var cfg = getConfig();
  if (!cfg.token || !cfg.vin) {
    sendError('Set token & VIN in app settings');
    return;
  }
  var url = API_BASE + '/' + encodeURIComponent(cfg.vin) + path;
  var xhr = new XMLHttpRequest();
  xhr.open(method, url, true);
  xhr.setRequestHeader('Authorization', 'Bearer ' + cfg.token);
  xhr.timeout = timeoutMs || 35000; // commands can take a while if the car is asleep
  xhr.onload = function () {
    if (xhr.status >= 200 && xhr.status < 300) {
      var data = null;
      try { data = JSON.parse(xhr.responseText); } catch (e) {}
      cb(null, data);
    } else if (xhr.status === 401) {
      cb('Auth failed — check token');
    } else {
      cb('HTTP ' + xhr.status);
    }
  };
  xhr.onerror = function () { cb('Network error'); };
  xhr.ontimeout = function () { cb('Timed out (car asleep?)'); };
  xhr.send();
}

// Tessie GET /{vin}/status -> { status: "asleep" | "waiting_for_sleep" | "awake" }.
// Keep these codes in sync with the AwakeStatus enum in logic.h.
function awakeCode(status) {
  switch (status) {
    case 'awake':             return 1;  // AWAKE_AWAKE
    case 'asleep':            return 0;  // AWAKE_ASLEEP
    case 'waiting_for_sleep': return 2;  // AWAKE_WAITING
    default:                  return -1; // AWAKE_UNKNOWN
  }
}

// Map Tessie charge_state.charging_state onto a ChargeState code (must match the
// ChargeState enum in logic.h). "Connected but idle" (Stopped/NoPower) and
// "Complete" both mean the cable is plugged in, which is what gates the Start/Stop
// control on the watch.
function chargeCode(status) {
  switch (status) {
    case 'Charging':     return 2;  // CHARGE_CHARGING
    case 'Starting':     return 2;  // about to charge -> treat as charging
    case 'Complete':     return 3;  // CHARGE_COMPLETE
    case 'Stopped':      return 1;  // CHARGE_STOPPED (plugged, idle)
    case 'NoPower':      return 1;  // plugged but no power -> idle
    case 'Disconnected': return 0;  // CHARGE_DISCONNECTED
    default:             return -1; // CHARGE_UNKNOWN
  }
}

// Map Tessie's exterior paint onto a PaintColor code (must match the PaintColor
// enum in logic.h). Tessie reports the paint two ways: a friendly
// vehicle_config.exterior_color string ("MidnightSilver", "DeepBlue", …) and a
// comma-separated option_codes string ("PPSW,PMNG,…"). Prefer the friendly name;
// fall back to the option code. Returns -1 (PAINT_UNKNOWN) when neither resolves,
// which leaves the watch on its default accent. Order matters: "MidnightCherryRed"
// must read as red (not the dark "Midnight…" grey), so red is matched first.
function paintCode(s) {
  var cfg = (s && s.vehicle_config) || {};
  var name = String(cfg.exterior_color || '').toLowerCase();
  if (name) {
    if (/cherry|red/.test(name))                return 0; // PAINT_RED
    if (/pearl|white/.test(name))               return 1; // PAINT_WHITE
    if (/black|obsidian/.test(name))            return 2; // PAINT_BLACK
    if (/blue/.test(name))                      return 5; // PAINT_BLUE
    if (/midnight|stealth|gr[ae]y/.test(name))  return 4; // PAINT_GREY (dark)
    if (/silver|quicksilver|titanium/.test(name)) return 3; // PAINT_SILVER (light)
  }
  var codes = String(cfg.option_codes || '').toUpperCase();
  if (codes) {
    if (/\b(PPMR|PR00|PR01|PRMR)\b/.test(codes)) return 0; // red
    if (/\b(PPSW|PBCW|PMWH)\b/.test(codes))      return 1; // white
    if (/\b(PBSB|PMBL)\b/.test(codes))           return 2; // black
    if (/\b(PPSB|PMNS)\b/.test(codes))           return 5; // blue
    if (/\b(PMNG|PN00)\b/.test(codes))           return 4; // dark grey
    if (/\b(PMSS|PN01|PMTG)\b/.test(codes))      return 3; // silver
  }
  return -1; // PAINT_UNKNOWN
}

// ---- State fetch ----
function refreshState() {
  // Power status is a separate, cheap endpoint that does NOT wake the car.
  tessie('GET', '/status', function (serr, st) {
    var awake = awakeCode(st && st.status);
    refreshVehicleData(awake);
  });
}

function refreshVehicleData(awake) {
  // A live read (use_cache=false) reaches out to the car, so it only works while
  // the car is awake; on a sleeping car it stalls and Tessie returns 408. Only
  // force a live read when /status says the car is awake — otherwise take
  // Tessie's cached snapshot, which returns last-known values immediately
  // without a timeout (and still drives the card + launcher glance). awake===1
  // is AWAKE_AWAKE; asleep/waiting/unknown all fall back to the cached read.
  var path = (awake === 1) ? '/state?use_cache=false' : '/state';
  tessie('GET', path, function (err, s) {
    if (err) { sendError(err); return; }
    if (!s) { sendError('No state'); return; }
    pushState(s, awake);
  });
}

// Map a Tessie /state object onto the watch's message keys and send it.
function pushState(s, awake) {
  var cfg = getConfig();
  var charge = s.charge_state || {};
  var climate = s.climate_state || {};
  var vehicle = s.vehicle_state || {};

  // User-set vehicle name. `display_name` is the top-level Tesla/Tessie field;
  // fall back to vehicle_state.vehicle_name. Truncate at a codepoint boundary so
  // multi-byte sequences (e.g. emoji) are never split mid-character.
  var name = s.display_name || vehicle.vehicle_name || '';
  name = Array.from(name).slice(0, 24).join('');

  var battery = charge.battery_level != null ? charge.battery_level : 0;
  // Tessie reports battery_range in miles; convert to km when the user prefers it.
  var rangeMi = charge.battery_range != null ? charge.battery_range : 0;
  var range = Math.round(cfg.useKm ? rangeMi * 1.609344 : rangeMi);
  var locked = !!vehicle.locked;
  var climateOn = !!climate.is_climate_on;

  var insideC = Math.round(climate.inside_temp != null ? climate.inside_temp : 0);
  var targetC = Math.round(climate.driver_temp_setting != null
    ? climate.driver_temp_setting : cfg.lastTarget);

  // remember target so +/- has a base even before next refresh
  localStorage.setItem('last_target', String(targetC));

  // Charging: state code, target limit, and (while charging) minutes to the
  // limit. Tessie's time_to_full_charge is in hours; convert to whole minutes.
  var charging = chargeCode(charge.charging_state);
  var chargeLimit = charge.charge_limit_soc != null ? charge.charge_limit_soc : -1;
  if (chargeLimit >= 0) localStorage.setItem('last_limit', String(chargeLimit));
  var chargeTime = -1;
  if (charging === 2 && charge.time_to_full_charge > 0) {
    chargeTime = Math.round(charge.time_to_full_charge * 60);
  }

  function maybeF(c) { return cfg.useFahrenheit ? Math.round(c * 9 / 5 + 32) : c; }

  var dict = {
    BATTERY:      battery,
    RANGE:        range,
    LOCKED:       locked ? 1 : 0,
    CLIMATE_ON:   climateOn ? 1 : 0,
    INSIDE_TEMP:  maybeF(insideC),
    TARGET_TEMP:  maybeF(targetC),
    ONLINE:       (s.state === 'online') ? 1 : 0,
    AWAKE:        awake,
    NAME:         name,
    CHARGING:     charging,
    CHARGE_LIMIT: chargeLimit,
    CHARGE_TIME:  chargeTime,
    DIST_UNIT:    cfg.useKm ? 1 : 0,
    SHOW_CLOCK:   cfg.showClock ? 1 : 0
  };

  // Only send the paint color when we can identify it, so the watch keeps its
  // default accent rather than being forced to a fallback on every refresh.
  var paint = paintCode(s);
  if (paint >= 0) dict.PAINT_COLOR = paint;

  sendToWatch(dict);

  // Mirror the freshest read onto the launcher glance so battery/lock show under
  // the app name without opening it. Driven from pushState so the glance updates
  // on the same path as the in-app card (initial refresh, manual refresh, and
  // post-command polls) and never drifts from what the watch shows.
  updateGlance({ name: name, battery: battery, range: range, locked: locked,
                 climateOn: climateOn, charging: charging, distKm: cfg.useKm });
}

// Build the one-line launcher glance subtitle from the values we just pushed to
// the watch. Pure + exported so it's unit-testable like the other helpers.
function buildGlanceSubtitle(v) {
  var parts = [];
  var head = v.battery + '%';
  if (v.range > 0) head += ' · ' + v.range + (v.distKm ? ' km' : ' mi');
  parts.push(head);
  if (v.charging === 2) parts.push('Charging');
  else if (v.charging === 3) parts.push('Charged');
  parts.push(v.locked ? 'Locked' : 'Unlocked');
  if (v.climateOn) parts.push('Climate on');
  return parts.join(' · ');
}

// The glance icon. PebbleKit JS *requires* layout.icon — a slice without it is
// rejected (the failure callback fires and nothing appears). This is our own
// sedan glyph, declared as publishedMedia "TESLA_GLANCE" in package.json and
// referenced here via the app:// scheme.
var GLANCE_ICON = 'app://images/TESLA_GLANCE';

// Publish (reload) the app's launcher glance. A single slice with no expiration
// so it persists until the next reload. appGlanceReload arrived with the
// AppGlance API (SDK 4.0); guard so older runtimes simply skip it.
function updateGlance(v) {
  if (typeof Pebble === 'undefined' || !Pebble.appGlanceReload) return;
  var slice = { layout: { icon: GLANCE_ICON, subtitleTemplateString: buildGlanceSubtitle(v) } };
  Pebble.appGlanceReload([slice],
    function () { console.log('appGlanceReload ok'); },
    function (e) { console.log('appGlanceReload failed: ' + JSON.stringify(e)); });
}

// After a command, the car's reported state can lag the change by a few seconds
// even with wait_for_completion=true, so a single read often echoes the OLD
// value. Re-read /state up to REFRESH_MAX_READS times, REFRESH_RETRY_MS apart,
// pushing each read to the watch and stopping early once `isSettled(state)` is
// true. The car is awake here (we just commanded it), so we skip the /status
// precheck and report AWAKE_AWAKE. `isSettled` is optional: when absent (e.g.
// frunk/trunk, which have no status row), the first read is enough.
function refreshAfterCommand(isSettled, readsLeft) {
  tessie('GET', '/state?use_cache=false', function (err, s) {
    if (err) { sendError(err); return; }
    if (!s) { sendError('No state'); return; }
    pushState(s, 1 /* AWAKE_AWAKE */);
    if (isSettled && !isSettled(s) && readsLeft > 1) {
      setTimeout(function () { refreshAfterCommand(isSettled, readsLeft - 1); }, REFRESH_RETRY_MS);
    }
  });
}

// Predicates describing the state a command is expected to produce.
function isLocked(s)    { return !!(s.vehicle_state && s.vehicle_state.locked); }
function isClimateOn(s) { return !!(s.climate_state && s.climate_state.is_climate_on); }
function isCharging(s)  { return !!(s.charge_state && s.charge_state.charging_state === 'Charging'); }

// ---- Command dispatch ----
// Tesla commands only succeed when the car is awake. Check status; if it isn't
// awake, POST /wake (Tessie blocks up to 90s, then returns result:false) before
// running `then`.
function ensureAwake(then) {
  tessie('GET', '/status', function (err, st) {
    if (err) { sendError(err); return; }
    if (st && st.status === 'awake') { then(); return; }
    sendToWatch({ STATUS: 'Waking…' });
    tessie('POST', '/wake', function (werr, wd) {
      if (werr) { sendError(werr); return; }
      if (wd && wd.result === false) { sendError('Wake timed out'); return; }
      then();
    }, 95000);
  });
}

function doCommand(path, okMsg, isSettled) {
  ensureAwake(function () {
    tessie('POST', path + '?wait_for_completion=true', function (err, data) {
      if (err) { sendError(err); return; }
      if (data && data.result === false) {
        sendError('Command rejected');
        return;
      }
      sendToWatch({ STATUS: okMsg });
      // re-read state until it reflects the command (Tesla's report can lag)
      setTimeout(function () {
        refreshAfterCommand(isSettled, REFRESH_MAX_READS);
      }, REFRESH_FIRST_MS);
    });
  });
}

function setTemperature(deltaC) {
  var cfg = getConfig();
  var base = cfg.lastTarget;            // stored in °C
  var next = Math.max(15, Math.min(28, base + deltaC));
  localStorage.setItem('last_target', String(next));
  ensureAwake(function () {
    // Tessie Set Temperatures expects Celsius via ?temperature=
    tessie('POST', '/command/set_temperatures?temperature=' + next, function (err, data) {
      if (err) { sendError(err); return; }
      // Confirm in the user's preferred unit (the target is tracked in °C).
      var shown = cfg.useFahrenheit ? Math.round(next * 9 / 5 + 32) : next;
      sendToWatch({ STATUS: 'Set ' + shown + (cfg.useFahrenheit ? '°F' : '°C') });
      setTimeout(function () {
        refreshAfterCommand(function (s) {
          return s.climate_state &&
            Math.round(s.climate_state.driver_temp_setting) === next;
        }, REFRESH_MAX_READS);
      }, REFRESH_FIRST_MS);
    });
  });
}

// Charge limit is nudged in 5% steps and clamped to [50,100] (Tesla's daily
// range). The current limit is tracked in localStorage (refreshed from each
// /state read) so +/- has a base before the next refresh, mirroring setTemperature.
function setChargeLimit(deltaPct) {
  var cfg = getConfig();
  var next = Math.max(50, Math.min(100, cfg.lastLimit + deltaPct));
  localStorage.setItem('last_limit', String(next));
  ensureAwake(function () {
    tessie('POST', '/command/set_charge_limit?percent=' + next, function (err, data) {
      if (err) { sendError(err); return; }
      if (data && data.result === false) { sendError('Command rejected'); return; }
      sendToWatch({ STATUS: 'Limit ' + next + '%' });
      setTimeout(function () {
        refreshAfterCommand(function (s) {
          return s.charge_state && s.charge_state.charge_limit_soc === next;
        }, REFRESH_MAX_READS);
      }, REFRESH_FIRST_MS);
    });
  });
}

// Explicit user-initiated wake (the "Wake" controls row). Unlike ensureAwake,
// this is the whole action: POST /wake, then refresh so the watch reflects the
// new power state (and the Wake row drops off the controls menu).
function wakeVehicle() {
  sendToWatch({ STATUS: 'Waking…' });
  tessie('POST', '/wake', function (err, wd) {
    if (err) { sendError(err); return; }
    if (wd && wd.result === false) { sendError('Wake timed out'); return; }
    sendToWatch({ STATUS: 'Awake' });
    refreshState();
  }, 95000);
}

function handleCommand(code) {
  switch (code) {
    case CMD.REFRESH:     refreshState(); break;
    case CMD.WAKE:        wakeVehicle(); break;
    case CMD.LOCK:        doCommand('/command/lock', 'Locked', isLocked); break;
    case CMD.UNLOCK:      doCommand('/command/unlock', 'Unlocked', function (s) { return !isLocked(s); }); break;
    case CMD.CLIMATE_ON:  doCommand('/command/start_climate', 'Climate on', isClimateOn); break;
    case CMD.CLIMATE_OFF: doCommand('/command/stop_climate', 'Climate off', function (s) { return !isClimateOn(s); }); break;
    case CMD.FRUNK:       doCommand('/command/activate_front_trunk', 'Frunk'); break;
    case CMD.TRUNK:       doCommand('/command/activate_rear_trunk', 'Trunk'); break;
    case CMD.CHARGE_PORT: doCommand('/command/open_charge_port', 'Charge port'); break;
    case CMD.CHARGE_START: doCommand('/command/start_charging', 'Charging', isCharging); break;
    case CMD.CHARGE_STOP:  doCommand('/command/stop_charging', 'Charge stopped', function (s) { return !isCharging(s); }); break;
    case CMD.LIMIT_UP:    setChargeLimit(+5); break;
    case CMD.LIMIT_DOWN:  setChargeLimit(-5); break;
    case CMD.TEMP_UP:     setTemperature(+1); break;
    case CMD.TEMP_DOWN:   setTemperature(-1); break;
    default: sendError('Unknown cmd ' + code);
  }
}

// ---- Pebble events ----
Pebble.addEventListener('ready', function () {
  console.log('Tesla Control JS ready');
  refreshState();
});

Pebble.addEventListener('appmessage', function (e) {
  var p = e.payload;
  if (p.CMD != null) handleCommand(p.CMD);
});

// ---- Configuration page ----
Pebble.addEventListener('showConfiguration', function () {
  var cfg = getConfig();
  // Pass current values so the page can prefill (token intentionally not echoed back fully)
  var base = 'data:text/html,' + encodeURIComponent(buildConfigHtml(cfg));
  Pebble.openURL(base);
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e.response) return;
  var cfg;
  try { cfg = JSON.parse(decodeURIComponent(e.response)); }
  catch (err) { try { cfg = JSON.parse(e.response); } catch (e2) { return; } }
  if (cfg.token)  localStorage.setItem('tessie_token', cfg.token);
  if (cfg.vin)    localStorage.setItem('tessie_vin', cfg.vin.trim().toUpperCase());
  localStorage.setItem('use_f', cfg.useFahrenheit ? '1' : '0');
  localStorage.setItem('use_km', cfg.useKm ? '1' : '0');
  localStorage.setItem('show_clock', cfg.showClock ? '1' : '0');
  refreshState();
});

// Inline config page (data URI keeps everything self-contained, no hosting needed).
function buildConfigHtml(cfg) {
  var hasToken = cfg.token ? 'true' : 'false';
  return '<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">' +
    '<style>' +
    'body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;margin:0;background:#0a0a0a;color:#f5f5f5}' +
    '.wrap{max-width:480px;margin:0 auto;padding:28px 22px}' +
    'h1{font-size:22px;font-weight:800;letter-spacing:-.02em;margin:0 0 4px}' +
    '.sub{color:#888;font-size:13px;margin:0 0 26px}' +
    'label{display:block;font-size:12px;text-transform:uppercase;letter-spacing:.08em;color:#9a9a9a;margin:18px 0 7px}' +
    'input[type=text],input[type=password]{width:100%;box-sizing:border-box;padding:13px 14px;border-radius:12px;border:1px solid #2a2a2a;background:#161616;color:#fff;font-size:16px}' +
    'input:focus{outline:none;border-color:#e31937}' +
    '.row{display:flex;align-items:center;gap:10px;margin-top:18px}' +
    '.row input{width:22px;height:22px;accent-color:#e31937}' +
    '.note{font-size:12px;color:#6f6f6f;margin-top:6px;line-height:1.45}' +
    'a{color:#e31937;text-decoration:none}' +
    'button{margin-top:30px;width:100%;padding:15px;border:none;border-radius:14px;background:#e31937;color:#fff;font-size:16px;font-weight:700;letter-spacing:.01em}' +
    'button:active{background:#b3122c}' +
    '</style></head><body><div class="wrap">' +
    '<h1>Tesla Control</h1>' +
    '<p class="sub">Connect via your Tessie account</p>' +
    '<label>Tessie API Token</label>' +
    '<input id="token" type="password" placeholder="' + (cfg.token ? '•••••• (saved)' : 'paste token') + '">' +
    '<p class="note">Get yours at <a href="https://dash.tessie.com/settings/api">dash.tessie.com/settings/api</a></p>' +
    '<label>Vehicle VIN</label>' +
    '<input id="vin" type="text" value="' + (cfg.vin || '') + '" placeholder="5YJ3...">' +
    '<div class="row"><input id="usef" type="checkbox" ' + (cfg.useFahrenheit ? 'checked' : '') + '><span>Show temperatures in °F</span></div>' +
    '<div class="row"><input id="usekm" type="checkbox" ' + (cfg.useKm ? 'checked' : '') + '><span>Show distance in km</span></div>' +
    '<div class="row"><input id="showclock" type="checkbox" ' + (cfg.showClock ? 'checked' : '') + '><span>Show clock on screen</span></div>' +
    '<button onclick="save()">Save</button>' +
    '</div><script>' +
    'function save(){' +
    'var t=document.getElementById("token").value;' +
    'var v=document.getElementById("vin").value;' +
    'var f=document.getElementById("usef").checked;' +
    'var k=document.getElementById("usekm").checked;' +
    'var c=document.getElementById("showclock").checked;' +
    'var out={vin:v,useFahrenheit:f,useKm:k,showClock:c};' +
    'if(t&&t.indexOf("•")===-1){out.token=t;}' + // only overwrite token if user typed a new one
    // Honor return_to when the platform supplies it (the emulator passes a
    // localhost capture URL); fall back to the pebblejs://close scheme that the
    // real phone app intercepts.
    'var rt=(location.href.match(/[?&]return_to=([^&#]*)/)||[])[1];' +
    'var base=rt?decodeURIComponent(rt):"pebblejs://close#";' +
    'location.href=base+encodeURIComponent(JSON.stringify(out));' +
    '}' +
    '</scr' + 'ipt></body></html>';
}

// Test-only export. pypkjs / JerryScript have no CommonJS `module`, so this
// guard is a no-op at runtime on the phone; it only activates under Node/Jest.
if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    CMD: CMD,
    getConfig: getConfig,
    tessie: tessie,
    refreshState: refreshState,
    doCommand: doCommand,
    setTemperature: setTemperature,
    setChargeLimit: setChargeLimit,
    handleCommand: handleCommand,
    ensureAwake: ensureAwake,
    awakeCode: awakeCode,
    chargeCode: chargeCode,
    paintCode: paintCode,
    buildConfigHtml: buildConfigHtml,
    buildGlanceSubtitle: buildGlanceSubtitle,
    updateGlance: updateGlance
  };
}
