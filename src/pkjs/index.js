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
  FRUNK: 7, TRUNK: 8, CHARGE_PORT: 9
};

var API_BASE = 'https://api.tessie.com';

// ---- Config persisted via localStorage (set by config page) ----
function getConfig() {
  return {
    token: localStorage.getItem('tessie_token') || '',
    vin: localStorage.getItem('tessie_vin') || '',
    useFahrenheit: localStorage.getItem('use_f') === '1',
    lastTarget: parseInt(localStorage.getItem('last_target') || '21', 10) // °C
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

// ---- State fetch ----
function refreshState() {
  // Power status is a separate, cheap endpoint that does NOT wake the car.
  tessie('GET', '/status', function (serr, st) {
    var awake = awakeCode(st && st.status);
    refreshVehicleData(awake);
  });
}

function refreshVehicleData(awake) {
  // use_cache=false ensures we get a live read where possible
  tessie('GET', '/state?use_cache=false', function (err, s) {
    if (err) { sendError(err); return; }
    if (!s) { sendError('No state'); return; }

    var cfg = getConfig();
    var charge = s.charge_state || {};
    var climate = s.climate_state || {};
    var vehicle = s.vehicle_state || {};

    var insideC = Math.round(climate.inside_temp != null ? climate.inside_temp : 0);
    var targetC = Math.round(climate.driver_temp_setting != null
      ? climate.driver_temp_setting : cfg.lastTarget);

    // remember target so +/- has a base even before next refresh
    localStorage.setItem('last_target', String(targetC));

    function maybeF(c) { return cfg.useFahrenheit ? Math.round(c * 9 / 5 + 32) : c; }

    sendToWatch({
      BATTERY:     charge.battery_level != null ? charge.battery_level : 0,
      RANGE:       charge.battery_range != null ? Math.round(charge.battery_range) : 0,
      LOCKED:      vehicle.locked ? 1 : 0,
      CLIMATE_ON:  climate.is_climate_on ? 1 : 0,
      INSIDE_TEMP: maybeF(insideC),
      TARGET_TEMP: maybeF(targetC),
      ONLINE:      (s.state === 'online') ? 1 : 0,
      AWAKE:       awake
    });
  });
}

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

function doCommand(path, okMsg) {
  ensureAwake(function () {
    tessie('POST', path + '?wait_for_completion=true', function (err, data) {
      if (err) { sendError(err); return; }
      if (data && data.result === false) {
        sendError('Command rejected');
        return;
      }
      sendToWatch({ STATUS: okMsg });
      // re-read state so the watch reflects reality
      setTimeout(refreshState, 800);
    });
  });
}

function setTemperature(deltaC) {
  var cfg = getConfig();
  var base = cfg.lastTarget;            // stored in °C
  var next = Math.max(15, Math.min(28, base + deltaC));
  localStorage.setItem('last_target', String(next));
  ensureAwake(function () {
    // Tessie Set Temperature expects Celsius via ?temperature=
    tessie('POST', '/command/set_temperature?temperature=' + next, function (err, data) {
      if (err) { sendError(err); return; }
      sendToWatch({ STATUS: 'Set ' + next + '°C' });
      setTimeout(refreshState, 800);
    });
  });
}

function handleCommand(code) {
  switch (code) {
    case CMD.REFRESH:     refreshState(); break;
    case CMD.LOCK:        doCommand('/command/lock', 'Locked'); break;
    case CMD.UNLOCK:      doCommand('/command/unlock', 'Unlocked'); break;
    case CMD.CLIMATE_ON:  doCommand('/command/start_climate', 'Climate on'); break;
    case CMD.CLIMATE_OFF: doCommand('/command/stop_climate', 'Climate off'); break;
    case CMD.FRUNK:       doCommand('/command/activate_front_trunk', 'Frunk'); break;
    case CMD.TRUNK:       doCommand('/command/activate_rear_trunk', 'Trunk'); break;
    case CMD.CHARGE_PORT: doCommand('/command/open_charge_port', 'Charge port'); break;
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
    '<button onclick="save()">Save</button>' +
    '</div><script>' +
    'function save(){' +
    'var t=document.getElementById("token").value;' +
    'var v=document.getElementById("vin").value;' +
    'var f=document.getElementById("usef").checked;' +
    'var out={vin:v,useFahrenheit:f};' +
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
    handleCommand: handleCommand,
    ensureAwake: ensureAwake,
    awakeCode: awakeCode,
    buildConfigHtml: buildConfigHtml
  };
}
