'use strict';
// tray namespace (spec/commands/tray.md). Per-app + OS-UI: under conformance the host records intent
// and drives events via the __click/__menuClick testability hooks (a real status item and real clicks
// are not headless-testable). Skips on any host that does not compose `tray`.

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert, assertEqual = h.assertEqual;
  function delay(ms) { return new Promise(function (r) { setTimeout(r, ms); }); }
  async function waitFor(cond, ms) {
    var start = Date.now();
    while (!cond()) { if (Date.now() - start > ms) throw new Error('timed out'); await delay(10); }
  }

  test('tray.set / tray.remove are accepted; a non-array menu rejects EINVAL', async function (engawa) {
    if (engawa.capabilities.indexOf('tray') < 0) return;   // per-app; skip where not composed
    assertEqual(await engawa.invoke('tray.set',
      { tooltip: 'engawa', menu: [{ id: 'open', label: 'Open' }, {}, { id: 'quit', label: 'Quit' }] }),
      null, 'set with a tooltip + menu (incl. a separator) is accepted');
    assertEqual(await engawa.invoke('tray.remove'), null, 'remove is accepted');
    var err = null;
    try { await engawa.invoke('tray.set', { menu: 'nope' }); } catch (e) { err = e; }
    assertEqual(err && err.code, 'EINVAL', 'a non-array menu rejects EINVAL');
  });

  test('tray.__click / __menuClick drive tray.clicked / tray.menuClicked events', async function (engawa) {
    if (engawa.capabilities.indexOf('tray') < 0) return;
    await engawa.invoke('tray.set', { menu: [{ id: 'open', label: 'Open' }] });
    var clicks = [], menuClicks = [];
    var offA = engawa.on('tray.clicked', function () { clicks.push(1); });
    var offB = engawa.on('tray.menuClicked', function (p) { menuClicks.push(p); });

    await engawa.invoke('tray.__click');
    await waitFor(function () { return clicks.length > 0; }, 2000);

    await engawa.invoke('tray.__menuClick', { id: 'open' });
    await waitFor(function () { return menuClicks.length > 0; }, 2000);

    offA(); offB();
    assertEqual(menuClicks[menuClicks.length - 1].id, 'open', 'tray.menuClicked carried the clicked id');
  });
})();
