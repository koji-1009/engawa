'use strict';
// window namespace (spec/commands/window.md). Covers size round-trip, basic command
// acceptance, and the close protocol (§4.2) end to end via the requestClose test hook —
// which also exercises real event delivery through shell.js on both hosts.

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert, assertEqual = h.assertEqual;

  function delay(ms) { return new Promise(function (r) { setTimeout(r, ms); }); }
  async function waitFor(cond, timeoutMs) {
    var start = Date.now();
    while (!cond()) {
      if (Date.now() - start > timeoutMs) throw new Error('timed out waiting for condition');
      await delay(10);
    }
  }

  test('window.setSize → getSize round-trip', async function (engawa) {
    assertEqual(await engawa.invoke('window.setSize', { width: 640, height: 480 }), null, 'setSize returns null');
    var size = await engawa.invoke('window.getSize');
    assertEqual(size.width, 640, 'width');
    assertEqual(size.height, 480, 'height');
  });

  test('window.setSize with bad args rejects EINVAL', async function (engawa) {
    var err = null;
    try { await engawa.invoke('window.setSize', { width: 640 }); } catch (e) { err = e; }
    assert(err, 'expected rejection');
    assertEqual(err.code, 'EINVAL', 'code');
  });

  test('window.setTitle / setResizable / minimize / maximize are accepted', async function (engawa) {
    assertEqual(await engawa.invoke('window.setTitle', { title: 'engawa — 窓' }), null, 'setTitle');
    assertEqual(await engawa.invoke('window.setResizable', { resizable: false }), null, 'setResizable');
    assertEqual(await engawa.invoke('window.setResizable', { resizable: true }), null, 'setResizable back');
    assertEqual(await engawa.invoke('window.minimize'), null, 'minimize');
    assertEqual(await engawa.invoke('window.maximize'), null, 'maximize');
  });

  test('window.setSize emits a window.resize event with the new size', async function (engawa) {
    var events = [];
    var off = engawa.on('window.resize', function (p) { events.push(p); });
    await engawa.invoke('window.setSize', { width: 640, height: 480 });
    await waitFor(function () { return events.length > 0; }, 2000);
    off();
    var last = events[events.length - 1];
    assertEqual(last.width, 640, 'resize width');
    assertEqual(last.height, 480, 'resize height');
  });

  test('§2.1 window.resize is coalesced to the latest per delivery batch', async function (engawa) {
    var events = [];
    var off = engawa.on('window.resize', function (p) { events.push(p); });
    var r = await engawa.invoke('window.__resizeStorm', { count: 8, from: 300 });
    await waitFor(function () { return events.length > 0; }, 2000);
    await delay(150);   // let any stragglers land
    off();
    assert(events.length < 8, 'coalesced to fewer than the 8 resizes fired (got ' + events.length + ')');
    assertEqual(events[events.length - 1].width, r.last, 'the latest size survives coalescing');
  });

  test('close protocol: closeRequested carries a token; respondToClose consumes it once', async function (engawa) {
    var events = [];
    var off = engawa.on('window.closeRequested', function (p) { events.push(p); });

    await engawa.invoke('window.requestClose');
    await waitFor(function () { return events.length > 0; }, 3000);
    off();

    var token = events[0] && events[0].token;
    assert(typeof token === 'number', 'closeRequested carried a numeric token');

    // Veto the close so nothing is destroyed mid-suite.
    assertEqual(await engawa.invoke('window.respondToClose', { token: token, allow: false }), null, 'respondToClose ok');

    // The token is single-use: answering again rejects.
    var err = null;
    try { await engawa.invoke('window.respondToClose', { token: token, allow: false }); } catch (e) { err = e; }
    assert(err, 'expected second respondToClose to reject');
    assertEqual(err.code, 'EINVAL', 'consumed token code');
  });
})();
