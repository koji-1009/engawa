'use strict';
// §10 renderer-crash recovery (contract §10, §11). A real renderer-death trigger isn't reliable
// on WKWebView (see the host's delegate comment), so a conformance-only hook runs the SAME
// recovery accounting + app.renderCrashed event the real delegate does. macOS-only — the mock
// host has no renderer.

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert, assertEqual = h.assertEqual;
  function delay(ms) { return new Promise(function (r) { setTimeout(r, ms); }); }
  async function waitFor(cond, t) { var s = Date.now(); while (!cond()) { if (Date.now() - s > t) throw new Error('timed out'); await delay(10); } }

  test('§10 renderer crash emits app.renderCrashed with an incrementing count; 3 in 60 s trips the guard', async function (engawa) {
    if (!engawa.simulateRenderCrash) return;   // requires a real renderer (macOS host)

    var events = [];
    var off = engawa.on('app.renderCrashed', function (p) { events.push(p); });

    var r1 = await engawa.simulateRenderCrash();
    await engawa.simulateRenderCrash();
    var r3 = await engawa.simulateRenderCrash();
    await waitFor(function () { return events.length >= 3; }, 3000);
    off();

    assert(typeof events[0].count === 'number', 'app.renderCrashed carries a numeric count');
    assertEqual(events[0].count, 1, 'first crash → count 1');
    assertEqual(events[1].count, 2, 'second crash → count 2');
    assertEqual(events[2].count, 3, 'third crash → count 3');
    assertEqual(r1.over, false, 'a single crash does not trip the guard');
    assertEqual(r3.over, true, 'three crashes within 60 s trip the guard (error screen instead of another reload)');
  });
})();
