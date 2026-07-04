'use strict';
// Wire limits (contract §11): a ~1 MiB message must survive the round-trip intact — the
// message channel is text/JSON and must not truncate or corrupt large frames.

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert, assertEqual = h.assertEqual;

  test('a ~1 MiB message round-trips intact (echo)', async function (engawa) {
    var big = new Array(1024 * 1024 + 1).join('x');   // ~1 MiB of 'x'
    assert(big.length >= 1024 * 1024, 'payload is at least 1 MiB');
    var got = await engawa.invoke('echo', { blob: big });
    assert(got && got.blob === big, 'the 1 MiB payload came back byte-for-byte');
  });

  test('a large message with mixed unicode survives', async function (engawa) {
    var unit = 'えんがわ🌇';
    var big = new Array(50000).join(unit);   // multibyte, well over 100 KiB
    var got = await engawa.invoke('echo', { s: big });
    assertEqual(got.s, big, 'mixed-unicode large payload intact');
  });
})();
