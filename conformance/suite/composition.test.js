'use strict';
// Per-app namespace composition (contract §3.1). A namespace is composed only if it is mandatory
// core (app, window, update, path) or the app declares it in engawa.json. `capabilities` MUST equal
// the composed set, so a namespace the app did not declare is neither advertised nor served —
// invoking it rejects locally with ENOTSUP (§1.1), never reaching the host. Requires a real host
// composed from a distinct fixture, so the mock skips.

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert, assertEqual = h.assertEqual;

  test('§3.1 an undeclared per-app namespace is not composed (absent from capabilities, local ENOTSUP)', async function (engawa) {
    if (!engawa.checkComposition) return;   // requires a real host + the minimal-fixture probe

    // This suite's app fixture DECLARES clipboard, so the primary host serves it.
    assert(engawa.capabilities.indexOf('clipboard') >= 0, 'the app that declares clipboard is served it');

    // A host built from a fixture that declares NO clipboard must neither advertise nor serve it.
    var r = await engawa.checkComposition();
    assert(r.capabilities.indexOf('clipboard') < 0, 'an undeclared namespace is absent from capabilities');
    assertEqual(r.clipboardCode, 'ENOTSUP', 'invoking the uncomposed namespace rejects locally with ENOTSUP');

    // The mandatory core is composed regardless of declaration.
    ['app', 'window', 'update', 'path'].forEach(function (ns) {
      assert(r.capabilities.indexOf(ns) >= 0, 'mandatory core namespace present without declaration: ' + ns);
    });
  });
})();
