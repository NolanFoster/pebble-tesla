// Hand-rolled fakes for the three browser-ish globals that PebbleKit JS relies
// on (localStorage, Pebble, XMLHttpRequest). Kept dependency-free so they also
// document the pypkjs runtime contract index.js expects.

function makeLocalStorage(initial) {
  var store = {};
  if (initial) {
    Object.keys(initial).forEach(function (k) { store[k] = String(initial[k]); });
  }
  return {
    getItem: function (k) {
      return Object.prototype.hasOwnProperty.call(store, k) ? store[k] : null;
    },
    setItem: function (k, v) { store[k] = String(v); },
    removeItem: function (k) { delete store[k]; },
    clear: function () { Object.keys(store).forEach(function (k) { delete store[k]; }); }
  };
}

function makePebble() {
  var listeners = {};
  return {
    listeners: listeners,
    addEventListener: function (ev, cb) {
      (listeners[ev] = listeners[ev] || []).push(cb);
    },
    emit: function (ev, e) {
      (listeners[ev] || []).forEach(function (cb) { cb(e); });
    },
    sendAppMessage: jest.fn(function (dict, ok, fail) { if (ok) ok(); }),
    appGlanceReload: jest.fn(function (slices, ok, fail) { if (ok) ok(); }),
    openURL: jest.fn()
  };
}

// Returns a constructor whose instances capture the request and expose helpers
// to drive the async callbacks deterministically.
function makeXHRClass() {
  var instances = [];

  function MockXHR() {
    this.headers = {};
    this.method = null;
    this.url = null;
    this.timeout = 0;
    this.status = 0;
    this.responseText = '';
    this.sent = false;
    this.onload = null;
    this.onerror = null;
    this.ontimeout = null;
    instances.push(this);
  }
  MockXHR.prototype.open = function (method, url) { this.method = method; this.url = url; };
  MockXHR.prototype.setRequestHeader = function (k, v) { this.headers[k] = v; };
  MockXHR.prototype.send = function () { this.sent = true; };

  // Test drivers:
  MockXHR.prototype.respond = function (status, body) {
    this.status = status;
    this.responseText = body == null ? ''
      : (typeof body === 'string' ? body : JSON.stringify(body));
    if (this.onload) this.onload();
  };
  MockXHR.prototype.fail = function () { if (this.onerror) this.onerror(); };
  MockXHR.prototype.fireTimeout = function () { if (this.ontimeout) this.ontimeout(); };

  MockXHR.instances = instances;
  MockXHR.last = function () { return instances[instances.length - 1]; };
  return MockXHR;
}

module.exports = { makeLocalStorage, makePebble, makeXHRClass };
