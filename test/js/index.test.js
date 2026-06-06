var path = require('path');
var mocks = require('./helpers/mocks');

var INDEX = path.resolve(__dirname, '../../src/pkjs/index.js');
var CONFIGURED = { tessie_token: 'tok_abc', tessie_vin: '5YJ3ABCDEF' };

// (Re)load index.js with a fresh set of globals. The module registers Pebble
// listeners and reads localStorage at require() time, so globals must exist
// first and the module cache must be reset per test.
function load(storage) {
  global.localStorage = mocks.makeLocalStorage(storage || {});
  global.Pebble = mocks.makePebble();
  global.XMLHttpRequest = mocks.makeXHRClass();
  global.console = { log: function () {} };
  jest.resetModules();
  return require(INDEX);
}

// Commands and refresh now first hit GET /{vin}/status to learn whether the car
// is awake. This responds to that precheck so the flow proceeds.
function ackStatus(status) {
  var xhr = global.XMLHttpRequest.last();
  expect(xhr.url).toContain('/status');
  xhr.respond(200, { status: status || 'awake' });
}

afterEach(function () {
  jest.useRealTimers();
});

describe('getConfig', function () {
  test('defaults when storage empty', function () {
    var m = load({});
    expect(m.getConfig()).toEqual({ token: '', vin: '', useFahrenheit: false, useKm: false, lastTarget: 21, lastLimit: 80 });
  });

  test('parses use_f and last_target', function () {
    var m = load({ use_f: '1', last_target: '25' });
    var c = m.getConfig();
    expect(c.useFahrenheit).toBe(true);
    expect(c.lastTarget).toBe(25);
  });
});

describe('awakeCode', function () {
  test('maps Tessie status strings to AwakeStatus codes', function () {
    var m = load(CONFIGURED);
    expect(m.awakeCode('awake')).toBe(1);
    expect(m.awakeCode('asleep')).toBe(0);
    expect(m.awakeCode('waiting_for_sleep')).toBe(2);
    expect(m.awakeCode(undefined)).toBe(-1);
    expect(m.awakeCode('nonsense')).toBe(-1);
  });
});

describe('chargeCode', function () {
  test('maps Tessie charging_state strings to ChargeState codes', function () {
    var m = load(CONFIGURED);
    expect(m.chargeCode('Charging')).toBe(2);
    expect(m.chargeCode('Starting')).toBe(2);     // about to charge
    expect(m.chargeCode('Complete')).toBe(3);
    expect(m.chargeCode('Stopped')).toBe(1);
    expect(m.chargeCode('NoPower')).toBe(1);       // plugged, idle
    expect(m.chargeCode('Disconnected')).toBe(0);
    expect(m.chargeCode(undefined)).toBe(-1);
    expect(m.chargeCode('nonsense')).toBe(-1);
  });
});

describe('paintCode', function () {
  function ext(name) { return { vehicle_config: { exterior_color: name } }; }
  function code(c) { return { vehicle_config: { option_codes: c } }; }

  test('maps friendly exterior_color names to PaintColor codes', function () {
    var m = load(CONFIGURED);
    expect(m.paintCode(ext('RedMulticoat'))).toBe(0);
    expect(m.paintCode(ext('UltraRed'))).toBe(0);
    expect(m.paintCode(ext('PearlWhite'))).toBe(1);
    expect(m.paintCode(ext('SolidBlack'))).toBe(2);
    expect(m.paintCode(ext('Obsidian Black'))).toBe(2);
    expect(m.paintCode(ext('DeepBlue'))).toBe(5);
    expect(m.paintCode(ext('MidnightSilver'))).toBe(4); // dark, despite "Silver"
    expect(m.paintCode(ext('StealthGrey'))).toBe(4);
    expect(m.paintCode(ext('SilverMetallic'))).toBe(3);
    expect(m.paintCode(ext('Quicksilver'))).toBe(3);
  });

  test('reads red before "Midnight" so cherry red is not mistaken for grey', function () {
    var m = load(CONFIGURED);
    expect(m.paintCode(ext('MidnightCherryRed'))).toBe(0);
  });

  test('falls back to option_codes when no friendly name', function () {
    var m = load(CONFIGURED);
    expect(m.paintCode(code('PBSB,SC04,DV2W'))).toBe(2); // solid black
    expect(m.paintCode(code('PPSW'))).toBe(1);           // pearl white
    expect(m.paintCode(code('PMNG'))).toBe(4);           // midnight silver (grey)
    expect(m.paintCode(code('PPSB'))).toBe(5);           // deep blue
  });

  test('returns -1 (unknown) when paint is absent or unrecognized', function () {
    var m = load(CONFIGURED);
    expect(m.paintCode({})).toBe(-1);
    expect(m.paintCode({ vehicle_config: {} })).toBe(-1);
    expect(m.paintCode(ext('Chartreuse'))).toBe(-1);
  });
});

describe('tessie', function () {
  test('builds URL, auth header and default timeout', function () {
    var m = load(CONFIGURED);
    m.tessie('POST', '/command/lock?x=1', function () {});
    var xhr = global.XMLHttpRequest.last();
    expect(xhr.method).toBe('POST');
    expect(xhr.url).toBe('https://api.tessie.com/5YJ3ABCDEF/command/lock?x=1');
    expect(xhr.headers['Authorization']).toBe('Bearer tok_abc');
    expect(xhr.timeout).toBe(35000);
    expect(xhr.sent).toBe(true);
  });

  test('honors an explicit timeout override', function () {
    var m = load(CONFIGURED);
    m.tessie('POST', '/wake', function () {}, 95000);
    expect(global.XMLHttpRequest.last().timeout).toBe(95000);
  });

  test('guards on missing token/vin and never opens a request', function () {
    var m = load({});
    var cb = jest.fn();
    m.tessie('GET', '/state', cb);
    expect(cb).not.toHaveBeenCalled();
    expect(global.XMLHttpRequest.instances.length).toBe(0);
    expect(global.Pebble.sendAppMessage).toHaveBeenCalledWith(
      { ERROR: 'Set token & VIN in app settings' },
      expect.any(Function), expect.any(Function));
  });

  test('maps HTTP status to callback values', function () {
    var m = load(CONFIGURED);

    var ok = jest.fn();
    m.tessie('GET', '/a', ok);
    global.XMLHttpRequest.last().respond(200, { hello: 1 });
    expect(ok).toHaveBeenCalledWith(null, { hello: 1 });

    var auth = jest.fn();
    m.tessie('GET', '/b', auth);
    global.XMLHttpRequest.last().respond(401, '');
    expect(auth).toHaveBeenCalledWith('Auth failed — check token');

    var err = jest.fn();
    m.tessie('GET', '/c', err);
    global.XMLHttpRequest.last().respond(500, '');
    expect(err).toHaveBeenCalledWith('HTTP 500');

    var neterr = jest.fn();
    m.tessie('GET', '/d', neterr);
    global.XMLHttpRequest.last().fail();
    expect(neterr).toHaveBeenCalledWith('Network error');

    var tmo = jest.fn();
    m.tessie('GET', '/e', tmo);
    global.XMLHttpRequest.last().fireTimeout();
    expect(tmo).toHaveBeenCalledWith('Timed out (car asleep?)');
  });
});

describe('ensureAwake', function () {
  test('runs the action immediately when already awake', function () {
    var m = load(CONFIGURED);
    var then = jest.fn();
    m.ensureAwake(then);
    var xhr = global.XMLHttpRequest.last();
    expect(xhr.url).toContain('/status');
    expect(then).not.toHaveBeenCalled();
    xhr.respond(200, { status: 'awake' });
    expect(then).toHaveBeenCalledTimes(1);
    expect(global.XMLHttpRequest.instances.length).toBe(1); // no wake needed
  });

  test('wakes first when asleep, then runs the action', function () {
    var m = load(CONFIGURED);
    var then = jest.fn();
    m.ensureAwake(then);
    global.XMLHttpRequest.last().respond(200, { status: 'asleep' });
    expect(global.Pebble.sendAppMessage).toHaveBeenCalledWith(
      { STATUS: 'Waking…' }, expect.any(Function), expect.any(Function));
    var wake = global.XMLHttpRequest.last();
    expect(wake.method).toBe('POST');
    expect(wake.url).toContain('/wake');
    expect(wake.timeout).toBe(95000); // Tessie blocks up to 90s
    expect(then).not.toHaveBeenCalled();
    wake.respond(200, { result: true });
    expect(then).toHaveBeenCalledTimes(1);
  });

  test('also wakes from waiting_for_sleep', function () {
    var m = load(CONFIGURED);
    var then = jest.fn();
    m.ensureAwake(then);
    global.XMLHttpRequest.last().respond(200, { status: 'waiting_for_sleep' });
    expect(global.XMLHttpRequest.last().url).toContain('/wake');
  });

  test('reports when wake times out (result:false)', function () {
    var m = load(CONFIGURED);
    var then = jest.fn();
    m.ensureAwake(then);
    global.XMLHttpRequest.last().respond(200, { status: 'asleep' });
    global.XMLHttpRequest.last().respond(200, { result: false });
    expect(then).not.toHaveBeenCalled();
    expect(global.Pebble.sendAppMessage).toHaveBeenCalledWith(
      { ERROR: 'Wake timed out' }, expect.any(Function), expect.any(Function));
  });
});

describe('refreshState', function () {
  var STATE = {
    state: 'online',
    display_name: 'Bumblebee',
    charge_state: { battery_level: 84, battery_range: 240.4 },
    climate_state: { inside_temp: 21, driver_temp_setting: 22, is_climate_on: true },
    vehicle_state: { locked: true }
  };

  test('reads /status then /state and keeps Celsius when useFahrenheit is off', function () {
    var m = load(CONFIGURED);
    m.refreshState();
    ackStatus('awake');
    var xhr = global.XMLHttpRequest.last();
    expect(xhr.method).toBe('GET');
    expect(xhr.url).toContain('/state?use_cache=false');
    xhr.respond(200, STATE);
    expect(global.Pebble.sendAppMessage).toHaveBeenCalledWith({
      BATTERY: 84, RANGE: 240, LOCKED: 1, CLIMATE_ON: 1,
      INSIDE_TEMP: 21, TARGET_TEMP: 22, ONLINE: 1, AWAKE: 1, NAME: 'Bumblebee',
      CHARGING: -1, CHARGE_LIMIT: -1, CHARGE_TIME: -1, DIST_UNIT: 0
    }, expect.any(Function), expect.any(Function));
    expect(global.localStorage.getItem('last_target')).toBe('22'); // stored in °C
  });

  test('forces a live read (use_cache=false) only when the car is awake', function () {
    var m = load(CONFIGURED);
    m.refreshState();
    ackStatus('awake');
    expect(global.XMLHttpRequest.last().url).toContain('/state?use_cache=false');
  });

  test('takes the cached read (no live read) when the car is asleep, avoiding a 408', function () {
    var m = load(CONFIGURED);
    m.refreshState();
    ackStatus('asleep');
    var xhr = global.XMLHttpRequest.last();
    expect(xhr.url).toContain('/state');
    expect(xhr.url).not.toContain('use_cache=false');
    // a cached snapshot still drives the card
    xhr.respond(200, STATE);
    expect(global.Pebble.sendAppMessage).toHaveBeenCalled();
  });

  test('takes the cached read when the awake status is unknown', function () {
    var m = load(CONFIGURED);
    m.refreshState();
    ackStatus('nonsense'); // -> AWAKE_UNKNOWN
    var xhr = global.XMLHttpRequest.last();
    expect(xhr.url).toContain('/state');
    expect(xhr.url).not.toContain('use_cache=false');
  });

  test('includes PAINT_COLOR when the car paint is identifiable', function () {
    var m = load(CONFIGURED);
    m.refreshState();
    ackStatus('awake');
    global.XMLHttpRequest.last().respond(200,
      Object.assign({ vehicle_config: { exterior_color: 'DeepBlue' } }, STATE));
    var dict = global.Pebble.sendAppMessage.mock.calls[0][0];
    expect(dict.PAINT_COLOR).toBe(5);
  });

  test('omits PAINT_COLOR when the paint is unknown (watch keeps its default)', function () {
    var m = load(CONFIGURED);
    m.refreshState();
    ackStatus('awake');
    global.XMLHttpRequest.last().respond(200, STATE); // no vehicle_config
    var dict = global.Pebble.sendAppMessage.mock.calls[0][0];
    expect('PAINT_COLOR' in dict).toBe(false);
  });

  test('forwards charging state, limit and minutes-to-full while charging', function () {
    var m = load(CONFIGURED);
    m.refreshState();
    ackStatus('awake');
    global.XMLHttpRequest.last().respond(200, Object.assign({}, STATE, {
      charge_state: {
        battery_level: 84, battery_range: 240.4,
        charging_state: 'Charging', charge_limit_soc: 90, time_to_full_charge: 1.5
      }
    }));
    var dict = global.Pebble.sendAppMessage.mock.calls[0][0];
    expect(dict.CHARGING).toBe(2);
    expect(dict.CHARGE_LIMIT).toBe(90);
    expect(dict.CHARGE_TIME).toBe(90);             // 1.5h -> 90 min
    expect(global.localStorage.getItem('last_limit')).toBe('90');
  });

  test('omits the charge ETA when not actively charging', function () {
    var m = load(CONFIGURED);
    m.refreshState();
    ackStatus('awake');
    global.XMLHttpRequest.last().respond(200, Object.assign({}, STATE, {
      charge_state: {
        battery_level: 84, battery_range: 240.4,
        charging_state: 'Complete', charge_limit_soc: 80, time_to_full_charge: 0
      }
    }));
    var dict = global.Pebble.sendAppMessage.mock.calls[0][0];
    expect(dict.CHARGING).toBe(3);
    expect(dict.CHARGE_LIMIT).toBe(80);
    expect(dict.CHARGE_TIME).toBe(-1);
  });

  test('reports the awake status independent of the state read', function () {
    var m = load(CONFIGURED);
    m.refreshState();
    ackStatus('waiting_for_sleep');
    global.XMLHttpRequest.last().respond(200, STATE);
    var dict = global.Pebble.sendAppMessage.mock.calls[0][0];
    expect(dict.AWAKE).toBe(2);
  });

  test('does NOT wake the car on a plain refresh', function () {
    var m = load(CONFIGURED);
    m.refreshState();
    ackStatus('asleep'); // asleep, but refresh must not POST /wake
    global.XMLHttpRequest.last().respond(200, STATE);
    var urls = global.XMLHttpRequest.instances.map(function (x) { return x.url; });
    expect(urls.some(function (u) { return /\/wake/.test(u); })).toBe(false);
  });

  test('converts to Fahrenheit when enabled', function () {
    var m = load(Object.assign({ use_f: '1' }, CONFIGURED));
    m.refreshState();
    ackStatus('awake');
    global.XMLHttpRequest.last().respond(200, STATE);
    var dict = global.Pebble.sendAppMessage.mock.calls[0][0];
    expect(dict.INSIDE_TEMP).toBe(70); // round(21*9/5+32)
    expect(dict.TARGET_TEMP).toBe(72); // round(22*9/5+32)=71.6→72
    expect(global.localStorage.getItem('last_target')).toBe('22'); // still °C
  });

  test('converts range to km and flags the unit when enabled', function () {
    var m = load(Object.assign({ use_km: '1' }, CONFIGURED));
    m.refreshState();
    ackStatus('awake');
    global.XMLHttpRequest.last().respond(200, STATE); // battery_range 240.4 mi
    var dict = global.Pebble.sendAppMessage.mock.calls[0][0];
    expect(dict.RANGE).toBe(387);     // round(240.4 * 1.609344)
    expect(dict.DIST_UNIT).toBe(1);
  });

  test('sends the vehicle name, preferring display_name', function () {
    var m = load(CONFIGURED);
    m.refreshState();
    ackStatus('awake');
    global.XMLHttpRequest.last().respond(200, STATE);
    var dict = global.Pebble.sendAppMessage.mock.calls[0][0];
    expect(dict.NAME).toBe('Bumblebee');
  });

  test('falls back to vehicle_state.vehicle_name, then empty', function () {
    var m = load(CONFIGURED);
    m.refreshState();
    ackStatus('awake');
    global.XMLHttpRequest.last().respond(200, {
      state: 'online', vehicle_state: { locked: true, vehicle_name: 'Optimus' }
    });
    expect(global.Pebble.sendAppMessage.mock.calls[0][0].NAME).toBe('Optimus');

    global.Pebble.sendAppMessage.mockClear();
    m.refreshState();
    ackStatus('awake');
    global.XMLHttpRequest.last().respond(200, {});
    expect(global.Pebble.sendAppMessage.mock.calls[0][0].NAME).toBe('');
  });

  test('truncates long names at a codepoint boundary (no split emoji)', function () {
    var m = load(CONFIGURED);
    m.refreshState();
    ackStatus('awake');
    // 30 rocket emoji; each is a surrogate pair in UTF-16. Expect 24 codepoints,
    // and every character intact (no lone surrogate).
    var longName = '🚀'.repeat(30);
    global.XMLHttpRequest.last().respond(200, { state: 'online', display_name: longName });
    var name = global.Pebble.sendAppMessage.mock.calls[0][0].NAME;
    expect(Array.from(name).length).toBe(24);
    expect(name).toBe('🚀'.repeat(24));
  });

  test('falls back to zeros and lastTarget on empty state', function () {
    var m = load(Object.assign({ last_target: '19' }, CONFIGURED));
    m.refreshState();
    ackStatus('asleep');
    global.XMLHttpRequest.last().respond(200, {});
    var dict = global.Pebble.sendAppMessage.mock.calls[0][0];
    expect(dict).toMatchObject({ BATTERY: 0, RANGE: 0, LOCKED: 0, CLIMATE_ON: 0, ONLINE: 0, AWAKE: 0 });
    expect(dict.TARGET_TEMP).toBe(19);
  });
});

describe('AppGlance', function () {
  var STATE = {
    state: 'online',
    display_name: 'Bumblebee',
    charge_state: { battery_level: 84, battery_range: 240.4 },
    climate_state: { inside_temp: 21, driver_temp_setting: 22, is_climate_on: true },
    vehicle_state: { locked: true }
  };

  describe('buildGlanceSubtitle', function () {
    test('battery + range + lock + climate', function () {
      var m = load(CONFIGURED);
      expect(m.buildGlanceSubtitle({ battery: 84, range: 240, locked: true, climateOn: true }))
        .toBe('84% · 240 mi · Locked · Climate on');
    });

    test('omits range when zero and shows Unlocked, no climate when off', function () {
      var m = load(CONFIGURED);
      expect(m.buildGlanceSubtitle({ battery: 50, range: 0, locked: false, climateOn: false }))
        .toBe('50% · Unlocked');
    });

    test('shows Charging / Charged after the battery when plugged in', function () {
      var m = load(CONFIGURED);
      expect(m.buildGlanceSubtitle({ battery: 60, range: 180, locked: true, climateOn: false, charging: 2 }))
        .toBe('60% · 180 mi · Charging · Locked');
      expect(m.buildGlanceSubtitle({ battery: 80, range: 200, locked: true, climateOn: false, charging: 3 }))
        .toBe('80% · 200 mi · Charged · Locked');
    });
  });

  test('refresh reloads the glance from the same values pushed to the watch', function () {
    var m = load(CONFIGURED);
    m.refreshState();
    ackStatus('awake');
    global.XMLHttpRequest.last().respond(200, STATE);
    expect(global.Pebble.appGlanceReload).toHaveBeenCalledTimes(1);
    var slices = global.Pebble.appGlanceReload.mock.calls[0][0];
    expect(slices).toEqual([
      { layout: {
        icon: 'app://images/TESLA_GLANCE',
        subtitleTemplateString: '84% · 240 mi · Locked · Climate on'
      } }
    ]);
  });

  test('updateGlance is a no-op when the runtime lacks appGlanceReload', function () {
    var m = load(CONFIGURED);
    delete global.Pebble.appGlanceReload;
    expect(function () {
      m.updateGlance({ battery: 84, range: 240, locked: true, climateOn: false });
    }).not.toThrow();
  });
});

describe('setTemperature', function () {
  test('clamps to [15,28]', function () {
    var hi = load(Object.assign({ last_target: '28' }, CONFIGURED));
    hi.setTemperature(+1);
    ackStatus('awake');
    expect(global.XMLHttpRequest.last().url).toContain('?temperature=28');
    expect(global.localStorage.getItem('last_target')).toBe('28');

    var lo = load(Object.assign({ last_target: '15' }, CONFIGURED));
    lo.setTemperature(-1);
    ackStatus('awake');
    expect(global.XMLHttpRequest.last().url).toContain('?temperature=15');
  });

  test('increments and confirms to the watch', function () {
    var m = load(Object.assign({ last_target: '21' }, CONFIGURED));
    m.setTemperature(+1);
    ackStatus('awake');
    var xhr = global.XMLHttpRequest.last();
    expect(xhr.method).toBe('POST');
    expect(xhr.url).toContain('/command/set_temperatures?temperature=22');
    xhr.respond(200, {});
    expect(global.Pebble.sendAppMessage).toHaveBeenCalledWith(
      { STATUS: 'Set 22°C' }, expect.any(Function), expect.any(Function));
  });

  test('confirms in °F when the user prefers Fahrenheit', function () {
    var m = load(Object.assign({ last_target: '21', use_f: '1' }, CONFIGURED));
    m.setTemperature(+1);
    ackStatus('awake');
    var xhr = global.XMLHttpRequest.last();
    expect(xhr.url).toContain('/command/set_temperatures?temperature=22'); // still sent in °C
    xhr.respond(200, {});
    expect(global.Pebble.sendAppMessage).toHaveBeenCalledWith(
      { STATUS: 'Set 72°F' }, expect.any(Function), expect.any(Function)); // 22°C -> 72°F
  });
});

describe('setChargeLimit', function () {
  test('steps by 5% and clamps to [50,100]', function () {
    var hi = load(Object.assign({ last_limit: '100' }, CONFIGURED));
    hi.setChargeLimit(+5);
    ackStatus('awake');
    expect(global.XMLHttpRequest.last().url).toContain('/command/set_charge_limit?percent=100');
    expect(global.localStorage.getItem('last_limit')).toBe('100');

    var lo = load(Object.assign({ last_limit: '50' }, CONFIGURED));
    lo.setChargeLimit(-5);
    ackStatus('awake');
    expect(global.XMLHttpRequest.last().url).toContain('percent=50');
  });

  test('increments from the stored limit and confirms to the watch', function () {
    var m = load(Object.assign({ last_limit: '80' }, CONFIGURED));
    m.setChargeLimit(+5);
    ackStatus('awake');
    var xhr = global.XMLHttpRequest.last();
    expect(xhr.method).toBe('POST');
    expect(xhr.url).toContain('/command/set_charge_limit?percent=85');
    xhr.respond(200, {});
    expect(global.Pebble.sendAppMessage).toHaveBeenCalledWith(
      { STATUS: 'Limit 85%' }, expect.any(Function), expect.any(Function));
  });
});

describe('doCommand', function () {
  test('rejects when result is false', function () {
    var m = load(CONFIGURED);
    m.doCommand('/command/lock', 'Locked');
    ackStatus('awake');
    var xhr = global.XMLHttpRequest.last();
    expect(xhr.url).toContain('/command/lock?wait_for_completion=true');
    xhr.respond(200, { result: false });
    expect(global.Pebble.sendAppMessage).toHaveBeenCalledWith(
      { ERROR: 'Command rejected' }, expect.any(Function), expect.any(Function));
  });

  test('confirms success then re-reads /state, stopping once it settles', function () {
    jest.useFakeTimers();
    var m = load(CONFIGURED);
    m.handleCommand(m.CMD.LOCK); // via handleCommand so the isLocked predicate is wired
    ackStatus('awake');
    global.XMLHttpRequest.last().respond(200, { result: true });
    expect(global.Pebble.sendAppMessage).toHaveBeenCalledWith(
      { STATUS: 'Locked' }, expect.any(Function), expect.any(Function));

    var before = global.XMLHttpRequest.instances.length;
    jest.advanceTimersByTime(800);
    var read = global.XMLHttpRequest.last();
    expect(global.XMLHttpRequest.instances.length).toBe(before + 1);
    expect(read.url).toContain('/state?use_cache=false'); // reads state directly, no /status
    // state now reflects the lock -> polling stops
    read.respond(200, { vehicle_state: { locked: true } });
    jest.advanceTimersByTime(5000);
    expect(global.XMLHttpRequest.instances.length).toBe(before + 1);
  });

  test('keeps polling while the car still reports the old value', function () {
    jest.useFakeTimers();
    var m = load(CONFIGURED);
    m.handleCommand(m.CMD.LOCK);
    ackStatus('awake');
    global.XMLHttpRequest.last().respond(200, { result: true });

    jest.advanceTimersByTime(800);
    // first read is stale (still unlocked) -> schedules another read
    global.XMLHttpRequest.last().respond(200, { vehicle_state: { locked: false } });
    var afterFirst = global.XMLHttpRequest.instances.length;
    jest.advanceTimersByTime(1500);
    expect(global.XMLHttpRequest.instances.length).toBe(afterFirst + 1);
    expect(global.XMLHttpRequest.last().url).toContain('/state?use_cache=false');
  });

  test('commands with no status row read state just once', function () {
    jest.useFakeTimers();
    var m = load(CONFIGURED);
    m.handleCommand(m.CMD.FRUNK); // frunk has no observable status row -> no predicate
    ackStatus('awake');
    global.XMLHttpRequest.last().respond(200, { result: true });
    jest.advanceTimersByTime(800);
    global.XMLHttpRequest.last().respond(200, {});
    var n = global.XMLHttpRequest.instances.length;
    jest.advanceTimersByTime(5000);
    expect(global.XMLHttpRequest.instances.length).toBe(n); // no further polling
  });

  test('wakes a sleeping car before sending the command', function () {
    var m = load(CONFIGURED);
    m.doCommand('/command/lock', 'Locked');
    global.XMLHttpRequest.last().respond(200, { status: 'asleep' }); // /status
    var wake = global.XMLHttpRequest.last();
    expect(wake.url).toContain('/wake');
    wake.respond(200, { result: true });
    expect(global.XMLHttpRequest.last().url).toContain('/command/lock?wait_for_completion=true');
  });
});

describe('handleCommand', function () {
  var cases = [
    [1, '/command/lock'],
    [2, '/command/unlock'],
    [3, '/command/start_climate'],
    [4, '/command/stop_climate'],
    [7, '/command/activate_front_trunk'],
    [8, '/command/activate_rear_trunk'],
    [9, '/command/open_charge_port'],
    [11, '/command/start_charging'],
    [12, '/command/stop_charging'],
    [0, '/state?use_cache=false'],
    [5, '/command/set_temperatures?temperature='],
    [6, '/command/set_temperatures?temperature='],
    [13, '/command/set_charge_limit?percent='],
    [14, '/command/set_charge_limit?percent=']
  ];

  cases.forEach(function (c) {
    test('code ' + c[0] + ' -> ' + c[1], function () {
      var m = load(Object.assign({ last_target: '21' }, CONFIGURED));
      m.handleCommand(c[0]);
      ackStatus('awake'); // every command (and refresh) first checks /status
      expect(global.XMLHttpRequest.last().url).toContain(c[1]);
    });
  });

  test('WAKE (10) posts /wake directly then refreshes state', function () {
    var m = load(CONFIGURED);
    m.handleCommand(m.CMD.WAKE);
    // No /status precheck — wake is the whole action.
    var wake = global.XMLHttpRequest.last();
    expect(wake.method).toBe('POST');
    expect(wake.url).toContain('/wake');
    wake.respond(200, { result: true });
    // After waking it refreshes: GET /status then GET /state.
    expect(global.XMLHttpRequest.last().url).toContain('/status');
  });

  test('WAKE (10) reports a timeout when wake returns result:false', function () {
    var m = load(CONFIGURED);
    m.handleCommand(m.CMD.WAKE);
    global.XMLHttpRequest.last().respond(200, { result: false });
    expect(global.Pebble.sendAppMessage).toHaveBeenCalledWith(
      { ERROR: 'Wake timed out' }, expect.any(Function), expect.any(Function));
  });

  test('unknown code reports an error and opens no request', function () {
    var m = load(CONFIGURED);
    m.handleCommand(99);
    expect(global.XMLHttpRequest.instances.length).toBe(0);
    expect(global.Pebble.sendAppMessage).toHaveBeenCalledWith(
      { ERROR: 'Unknown cmd 99' }, expect.any(Function), expect.any(Function));
  });
});

describe('buildConfigHtml', function () {
  test('masks a saved token and prefills vin / fahrenheit', function () {
    var m = load({});
    var html = m.buildConfigHtml({ token: 'secret-xyz', vin: '5YJ3', useFahrenheit: true });
    expect(html).toContain('(saved)');
    expect(html).not.toContain('secret-xyz');
    expect(html).toContain('value="5YJ3"');
    expect(html).toContain('checked');
  });

  test('prompts to paste when no token saved', function () {
    var m = load({});
    var html = m.buildConfigHtml({ token: '', vin: '', useFahrenheit: false });
    expect(html).toContain('paste token');
  });

  test('honors return_to with a pebblejs://close fallback', function () {
    var m = load({});
    var html = m.buildConfigHtml({ token: '', vin: '', useFahrenheit: false });
    // emulator passes a localhost capture URL via return_to; real phone uses the scheme
    expect(html).toContain('return_to');
    expect(html).toContain('pebblejs://close#');
  });
});
