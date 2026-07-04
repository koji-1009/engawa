'use strict';
// notification namespace (spec/commands/notification.md). Record mode under conformance.

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert, assertEqual = h.assertEqual;

  test('notification.show records title and body', async function (engawa) {
    assertEqual(await engawa.invoke('notification.show', { title: '保存しました', body: 'ノートを更新' }), null, 'returns null');
    var rec = await engawa.invoke('notification.__recorded');
    assert(Array.isArray(rec) && rec.length > 0, 'recorded');
    var last = rec[rec.length - 1];
    assertEqual(last.title, '保存しました', 'title');
    assertEqual(last.body, 'ノートを更新', 'body');
  });

  test('notification.show defaults body to empty string', async function (engawa) {
    await engawa.invoke('notification.show', { title: 'title only' });
    var rec = await engawa.invoke('notification.__recorded');
    var last = rec[rec.length - 1];
    assertEqual(last.body, '', 'body defaults to ""');
  });

  test('notification.show without title rejects EINVAL', async function (engawa) {
    var err = null;
    try { await engawa.invoke('notification.show', { body: 'no title' }); } catch (e) { err = e; }
    assert(err, 'expected rejection');
    assertEqual(err.code, 'EINVAL', 'code');
  });
})();
