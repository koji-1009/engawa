'use strict';
// Out-of-range numeric arguments must not crash the host. Any args field typed as a JS number
// on the page side and narrowed to a native integer on the host side (pid, maxBytes, close
// token, …) can carry a value far outside the native int range. A trapping narrowing (Swift
// `Int(Double)`, C++ `static_cast<int>` of an out-of-range/NaN double) would kill the whole
// host process — a JS-reachable denial of service. The host MUST clamp such values and return
// a normal error, then keep serving. (NaN can't be tested over the wire: JSON has no NaN, so
// JSON.stringify(NaN) === 'null'; the clamp's NaN branch is covered by the native unit path.)

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert, assertEqual = h.assertEqual;

  test('out-of-range numeric args are clamped, not fatal (host survives)', async function (engawa) {
    var HUGE = 1e300;   // ~2^997: far beyond any native int; a trapping narrowing would crash here.

    async function expectCode(promise, code, what) {
      var err = null;
      try { await promise; } catch (e) { err = e; }
      assert(err && err.code === code, what + ': expected ' + code + ', got ' + (err && err.code));
    }

    // process.* pid narrowings. `read` also narrows maxBytes (clamped before the pid lookup), so
    // { pid, maxBytes } both out of range exercises two clamps in one call.
    await expectCode(engawa.invoke('process.read', { pid: HUGE, maxBytes: HUGE }), 'ESRCH', 'process.read pid+maxBytes=1e300');
    await expectCode(engawa.invoke('process.kill', { pid: HUGE }), 'ESRCH', 'process.kill pid=1e300');
    await expectCode(engawa.invoke('process.stdinWrite', { pid: HUGE, data: 'x' }), 'ESRCH', 'process.stdinWrite pid=1e300');
    await expectCode(engawa.invoke('process.stdinClose', { pid: HUGE }), 'ESRCH', 'process.stdinClose pid=1e300');

    // window.respondToClose token narrowing → unknown token → EINVAL (contract §4.2).
    await expectCode(engawa.invoke('window.respondToClose', { token: HUGE, allow: true }), 'EINVAL', 'window.respondToClose token=1e300');

    // The host must still be alive and serving after every clamp: a normal command round-trips.
    var got = await engawa.invoke('echo', { ping: 'alive' });
    assertEqual(got, { ping: 'alive' }, 'host still responds after out-of-range input (did not crash)');
  });
})();
