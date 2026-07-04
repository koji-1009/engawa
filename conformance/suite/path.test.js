'use strict';
// path namespace (spec/commands/path.md). Standard directories are non-empty absolute
// strings, stable within a run; unknown command in a served namespace → ENOSYS.

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert, assertEqual = h.assertEqual;

  var COMMANDS = ['appData', 'appConfig', 'appCache', 'home', 'temp'];

  test('path.* return non-empty absolute strings', async function (engawa) {
    for (var i = 0; i < COMMANDS.length; i++) {
      var p = await engawa.invoke('path.' + COMMANDS[i]);
      assert(typeof p === 'string' && p.length > 0, COMMANDS[i] + ' should be a non-empty string, got ' + JSON.stringify(p));
      assert(p.charAt(0) === '/', COMMANDS[i] + ' should be absolute, got ' + p);
    }
  });

  test('path.appData is stable across calls', async function (engawa) {
    var a = await engawa.invoke('path.appData');
    var b = await engawa.invoke('path.appData');
    assertEqual(a, b, 'appData must be stable within a run');
  });

  test('unknown command in a served namespace rejects with ENOSYS', async function (engawa) {
    var err = null;
    try { await engawa.invoke('path.bogusCommand'); } catch (e) { err = e; }
    assert(err, 'expected rejection');
    assertEqual(err.code, 'ENOSYS', 'rejection code');
  });
})();
