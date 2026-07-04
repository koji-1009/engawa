'use strict';
// Default CSP (contract §7.3): inline script is dead by default. This is a renderer
// property, so it applies only to hosts with a real engine — the Node mock host has no
// DOM/CSP and is skipped (per §11 that is not a smuggled engine-ism; it is the absence of
// a renderer, and the requirement is on the asset-serving layer, not the wire protocol).

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert;

  test('§7.3 inline <script> is dead, but external app: scripts load, under the default CSP', async function (engawa) {
    if (engawa.platform === 'mock') return;   // no renderer to enforce CSP
    assert(engawa.inlineScriptBlocked === true, 'an inline <script> must not run (no unsafe-inline)');
    assert(engawa.externalScriptRan === true, 'an external app: <script> must load (script-src app:)');
  });
})();
