'use strict';
// shellOpen namespace (spec/commands/shellOpen.md). Under conformance the host records
// requests instead of launching anything; we assert the recorded intent.

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert, assertEqual = h.assertEqual;

  test('shellOpen.openExternal records the url', async function (engawa) {
    var url = 'https://example.com/えんがわ?q=1';
    assertEqual(await engawa.invoke('shellOpen.openExternal', { url: url }), null, 'returns null');
    var rec = await engawa.invoke('shellOpen.__recorded');
    assert(Array.isArray(rec) && rec.length > 0, 'recorded is a non-empty array');
    var last = rec[rec.length - 1];
    assertEqual(last.action, 'openExternal', 'action');
    assertEqual(last.url, url, 'url captured verbatim');
  });

  test('shellOpen.revealInFolder records the path', async function (engawa) {
    var p = await engawa.invoke('path.appData');
    assertEqual(await engawa.invoke('shellOpen.revealInFolder', { path: p }), null, 'returns null');
    var rec = await engawa.invoke('shellOpen.__recorded');
    var last = rec[rec.length - 1];
    assertEqual(last.action, 'revealInFolder', 'action');
    assertEqual(last.path, p, 'path captured');
  });

  test('shellOpen.openExternal without url rejects EINVAL', async function (engawa) {
    var err = null;
    try { await engawa.invoke('shellOpen.openExternal', {}); } catch (e) { err = e; }
    assert(err, 'expected rejection');
    assertEqual(err.code, 'EINVAL', 'code');
  });
})();
