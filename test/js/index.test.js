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

describe('tessie', function () {
  test('builds URL, auth header and timeout', function () {
    var m = load(CONFIGURED);
    m.tessie('POST', '/command/lock?x=1', function () {});
    var xhr = global.XMLHttpRequest.last();
    expect(xhr.method).toBe('POST');
    expect(xhr.url).toBe('https://api.tessie.com/5YJ3ABCDEF/command/lock?x=1');
    expect(xhr.headers['Authorization']).toBe('Bearer tok_abc');
    expect(xhr.timeout).toBe(35000);
    expect(xhr.sent).toBe(true);
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

describe('refreshState', function () {
  var STATE = {
    state: 'online',
    charge_state: { battery_level: 84, battery_range: 240.4 },
    climate_state: { inside_temp: 21, driver_temp_setting: 22, is_climate_on: true },
    vehicle_state: { locked: true }
  };

  test('parses state and keeps Celsius when useFahrenheit is off', function () {
    var m = load(CONFIGURED);
    m.refreshState();
    var xhr = global.XMLHttpRequest.last();
    expect(xhr.method).toBe('GET');
    expect(xhr.url).toContain('/state?use_cache=false');
    xhr.respond(200, STATE);
    expect(global.Pebble.sendAppMessage).toHaveBeenCalledWith({
      BATTERY: 84, RANGE: 240, LOCKED: 1, CLIMATE_ON: 1,
      INSIDE_TEMP: 21, TARGET_TEMP: 22, ONLINE: 1
    }, expect.any(Function), expect.any(Function));
    expect(global.localStorage.getItem('last_target')).toBe('22'); // stored in °C
  });

  test('converts to Fahrenheit when enabled', function () {
    var m = load(Object.assign({ use_f: '1' }, CONFIGURED));
    m.refreshState();
    global.XMLHttpRequest.last().respond(200, STATE);
    var dict = global.Pebble.sendAppMessage.mock.calls[0][0];
    expect(dict.INSIDE_TEMP).toBe(70); // round(21*9/5+32)
    expect(dict.TARGET_TEMP).toBe(72); // round(22*9/5+32)=71.6→72
    expect(global.localStorage.getItem('last_target')).toBe('22'); // still °C
  });

  test('falls back to zeros and lastTarget on empty state', function () {
    var m = load(Object.assign({ last_target: '19' }, CONFIGURED));
    m.refreshState();
    global.XMLHttpRequest.last().respond(200, {});
    var dict = global.Pebble.sendAppMessage.mock.calls[0][0];
    expect(dict).toMatchObject({ BATTERY: 0, RANGE: 0, LOCKED: 0, CLIMATE_ON: 0, ONLINE: 0 });
    expect(dict.TARGET_TEMP).toBe(19);
  });
});

describe('setTemperature', function () {
  test('clamps to [15,28]', function () {
    var hi = load(Object.assign({ last_target: '28' }, CONFIGURED));
    hi.setTemperature(+1);
    expect(global.XMLHttpRequest.last().url).toContain('?temperature=28');
    expect(global.localStorage.getItem('last_target')).toBe('28');

    var lo = load(Object.assign({ last_target: '15' }, CONFIGURED));
    lo.setTemperature(-1);
    expect(global.XMLHttpRequest.last().url).toContain('?temperature=15');
  });

  test('increments and confirms to the watch', function () {
    var m = load(Object.assign({ last_target: '21' }, CONFIGURED));
    m.setTemperature(+1);
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
    global.XMLHttpRequest.last().respond(200, { result: true });
    expect(global.Pebble.sendAppMessage).toHaveBeenCalledWith(
      { STATUS: 'Locked' }, expect.any(Function), expect.any(Function));

    var before = global.XMLHttpRequest.instances.length;
    jest.advanceTimersByTime(800);
    var after = global.XMLHttpRequest.instances.length;
    expect(after).toBe(before + 1); // refreshState fired a new GET
    expect(global.XMLHttpRequest.last().url).toContain('/state?use_cache=false');
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
});
