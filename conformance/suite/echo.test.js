'use strict';
// Vertical slice (bootstrap stage 2): one request/response round-trip through the
// full wire protocol — invoke → postMessage → host handler → _deliver → promise.
// `echo` is a throwaway command for the slice; stage 3 re-lands it as an adapter.

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assertEqual = h.assertEqual;

  test('echo returns its args unchanged', async function (engawa) {
    var sent = { hello: 'world', n: 42, nested: { ok: true }, list: [1, 2, 3] };
    var got = await engawa.invoke('echo', sent);
    assertEqual(got, sent, 'echo round-trip');
  });

  test('echo preserves unicode integrity', async function (engawa) {
    var sent = { s: 'えんがわ 🌇 café — naïve' };
    var got = await engawa.invoke('echo', sent);
    assertEqual(got, sent, 'unicode round-trip');
  });
})();
