'use strict';
// Injection matrix (contract §6, §11): __shell + shell.js go into top-level app:// documents
// ONLY — never into iframes. Remote content in a frame must not reach commands. Verified on a
// real renderer (the mock host has none and skips).

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert;

  function delay(ms) { return new Promise(function (r) { setTimeout(r, ms); }); }

  test('§6 __shell is NOT injected into an app:// iframe', async function (engawa) {
    if (!engawa.frameCheck) return;   // requires a real renderer
    var fc, deadline = Date.now() + 3000;
    do { fc = await engawa.frameCheck(); if (!fc.iframeLoaded) await delay(50); } while (!fc.iframeLoaded && Date.now() < deadline);

    assert(fc.iframeLoaded === true, 'the iframe ran its external app: script (loaded, CSP allows subframe app: scripts)');
    assert(fc.iframeHadShell === false, 'the iframe did not receive __shell (§6: top-level app:// only)');
    assert(fc.topHasShell === true, 'the top-level app:// document did receive __shell');
  });
})();
