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

  function assertEqual(actual, expected, msg) {
    var a = JSON.stringify(actual);
    var e = JSON.stringify(expected);
    if (a !== e) throw new Error((msg || 'assertEqual') + ': expected ' + e + ', got ' + a);
  }

  var api = { test: test, tests: tests, assert: assert, assertEqual: assertEqual };

  global.engawaConformance = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
