'use strict';
// dialog namespace (spec/commands/dialog.md). Conformance uses preprogrammed responses
// (dialog.__setResponse); we assert the plumbing and argument validation.

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert, assertEqual = h.assertEqual;

  test('dialog.open returns the preprogrammed response verbatim', async function (engawa) {
    var resp = { canceled: false, paths: ['/tmp/a.txt', '/tmp/b.txt'] };
    await engawa.invoke('dialog.__setResponse', resp);
    var r = await engawa.invoke('dialog.open', { multiple: true });
    assertEqual(r, resp, 'open returns the set response');
  });

  test('dialog.save returns the preprogrammed response verbatim', async function (engawa) {
    var resp = { canceled: false, path: '/tmp/save.txt' };
    await engawa.invoke('dialog.__setResponse', resp);
    var r = await engawa.invoke('dialog.save', { defaultName: 'save.txt' });
    assertEqual(r, resp, 'save returns the set response');
  });

  test('dialog defaults to a canceled result when none is preset', async function (engawa) {
    var r = await engawa.invoke('dialog.open', {});
    assertEqual(r.canceled, true, 'defaults to canceled');
  });

  test('dialog.message without message rejects EINVAL', async function (engawa) {
    var err = null;
    try { await engawa.invoke('dialog.message', {}); } catch (e) { err = e; }
    assert(err, 'expected rejection');
    assertEqual(err.code, 'EINVAL', 'code');
  });
})();
