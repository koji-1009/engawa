'use strict';
// Storage durability (contract §10, §11): WebView-managed storage is cache — the suite wipes
// it between runs and the app must still boot. Durable data lives in fs/sqlite, not in
// IndexedDB/localStorage/caches. Requires a real host (the mock host skips).

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert;

  test('§10 the app boots after WebView storage is wiped', async function (engawa) {
    if (!engawa.checkStorageWipe) return;   // requires a real host
    var r = await engawa.checkStorageWipe();
    assert(r.booted === true, 'wiping IndexedDB/localStorage/caches must not stop the app booting');
  });
})();
