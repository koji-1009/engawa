'use strict';
// Namespace gating (contract §1.1): invoking a command whose namespace the host does
// not serve rejects locally with ENOTSUP — no round-trip. A round-trip would instead
// surface the host's unknown-command error, so the code proves the local short-circuit.

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert, assertEqual = h.assertEqual;

  test('invoking an unserved namespace rejects with ENOTSUP', async function (engawa) {
    var err = null;
    try {
      await engawa.invoke('nope.doThing', { x: 1 });
    } catch (e) {
      err = e;
    }
    assert(err, 'expected the invoke to reject');
    assertEqual(err.code, 'ENOTSUP', 'rejection code');
  });

  test('the engawa runtime surface is frozen', async function (engawa) {
    assert(Object.isFrozen(engawa), 'engawa must be frozen');
  });
})();
