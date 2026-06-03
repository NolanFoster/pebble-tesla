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
    expect(m.getConfig()).toEqual({ token: '', vin: '', useFahrenheit: false, lastTarget: 21 });
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
      INSIDE_TEMP: 21, TARGET_TEMP: 22, ONLINE: 1, AWAKE: 1
    }, expect.any(Function), expect.any(Function));
    expect(global.localStorage.getItem('last_target')).toBe('22'); // stored in °C
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
    expect(xhr.url).toContain('/command/set_temperature?temperature=22');
    xhr.respond(200, {});
    expect(global.Pebble.sendAppMessage).toHaveBeenCalledWith(
      { STATUS: 'Set 22°C' }, expect.any(Function), expect.any(Function));
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

  test('confirms success then schedules a refresh', function () {
    jest.useFakeTimers();
    var m = load(CONFIGURED);
    m.doCommand('/command/lock', 'Locked');
    ackStatus('awake');
    global.XMLHttpRequest.last().respond(200, { result: true });
    expect(global.Pebble.sendAppMessage).toHaveBeenCalledWith(
      { STATUS: 'Locked' }, expect.any(Function), expect.any(Function));

    var before = global.XMLHttpRequest.instances.length;
    jest.advanceTimersByTime(800);
    var after = global.XMLHttpRequest.instances.length;
    expect(after).toBe(before + 1); // refreshState fired a new GET (the /status precheck)
    expect(global.XMLHttpRequest.last().url).toContain('/status');
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
    [0, '/state?use_cache=false'],
    [5, '/command/set_temperature?temperature='],
    [6, '/command/set_temperature?temperature=']
  ];

  cases.forEach(function (c) {
    test('code ' + c[0] + ' -> ' + c[1], function () {
      var m = load(Object.assign({ last_target: '21' }, CONFIGURED));
      m.handleCommand(c[0]);
      ackStatus('awake'); // every command (and refresh) first checks /status
      expect(global.XMLHttpRequest.last().url).toContain(c[1]);
    });
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
