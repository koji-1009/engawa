'use strict';
// clipboard namespace (spec/commands/clipboard.md). The macOS host uses a private
// pasteboard under conformance, so this never touches the real clipboard.

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert, assertEqual = h.assertEqual;

  test('clipboard writeText → readText round-trip (unicode intact)', async function (engawa) {
    var s = 'engawa clip 📋 日本語 — line';
    assertEqual(await engawa.invoke('clipboard.writeText', { text: s }), null, 'writeText returns null');
    assertEqual(await engawa.invoke('clipboard.readText'), s, 'readText returns what was written');
  });

  test('clipboard.writeText without text rejects EINVAL', async function (engawa) {
    var err = null;
    try { await engawa.invoke('clipboard.writeText', {}); } catch (e) { err = e; }
    assert(err, 'expected rejection');
    assertEqual(err.code, 'EINVAL', 'code');
  });
})();
