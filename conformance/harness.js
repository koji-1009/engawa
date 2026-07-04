'use strict';
// Host-agnostic test harness, dual-mode so the SAME suite source runs both in Node
// (mock host, loaded via require/vm) and in the browser (macOS host, loaded via
// <script> into an app:// page against the real in-page shell.js).
//
// A suite module registers tests with `test(name, fn)`; the runner executes every
// registered test against each configured host, passing that host's `engawa` to `fn`.

;(function (global) {
  var tests = [];

  function test(name, fn) {
    tests.push({ name: name, fn: fn });
  }

  function assert(cond, msg) {
    if (!cond) throw new Error(msg || 'assertion failed');
  }

  // Structural, key-order-insensitive equality. JSON objects are unordered, and hosts
  // that route values through native map types (e.g. macOS NSDictionary) do not preserve
  // key order — comparing by JSON.stringify would spuriously fail.
  function deepEqual(a, b) {
    if (a === b) return true;
    if (typeof a !== typeof b) return false;
    if (a === null || b === null) return a === b;
    if (Array.isArray(a) || Array.isArray(b)) {
      if (!Array.isArray(a) || !Array.isArray(b) || a.length !== b.length) return false;
      for (var i = 0; i < a.length; i++) if (!deepEqual(a[i], b[i])) return false;
      return true;
    }
    if (typeof a === 'object') {
      var ka = Object.keys(a), kb = Object.keys(b);
      if (ka.length !== kb.length) return false;
      for (var j = 0; j < ka.length; j++) {
        if (!Object.prototype.hasOwnProperty.call(b, ka[j])) return false;
        if (!deepEqual(a[ka[j]], b[ka[j]])) return false;
      }
      return true;
    }
    return false;
  }

  function assertEqual(actual, expected, msg) {
    if (!deepEqual(actual, expected)) {
      throw new Error((msg || 'assertEqual') + ': expected ' + JSON.stringify(expected) + ', got ' + JSON.stringify(actual));
    }
  }

  var api = { test: test, tests: tests, assert: assert, assertEqual: assertEqual };

  global.engawaConformance = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
