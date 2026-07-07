'use strict';
// Per-app namespace composition (contract §3.1). A namespace is composed only if it is mandatory
// core (app, window, update, path) or the app declares it in engawa.json. `capabilities` MUST equal
// the composed set, so a namespace the app did not declare is neither advertised nor served —
// invoking it rejects locally with ENOTSUP (§1.1), never reaching the host. Requires a real host
// composed from a distinct fixture, so the mock skips.

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert, assertEqual = h.assertEqual;

  // The per-app namespaces the suite's app fixture declares (spec §3.1). None is mandatory core.
  var PER_APP = ['fs', 'clipboard', 'dialog', 'shellOpen', 'notification', 'process'];
  var CORE = ['app', 'window', 'update', 'path'];

  test('§3.1 declared per-app namespaces are served; undeclared ones are not (absent + local ENOTSUP)', async function (engawa) {
    if (!engawa.checkComposition) return;   // requires a real host + the minimal-fixture probe

    // This suite's app fixture DECLARES every per-app namespace it uses, so the primary host serves them.
    PER_APP.forEach(function (ns) {
      assert(engawa.capabilities.indexOf(ns) >= 0, 'declared per-app namespace is served: ' + ns);
    });

    // A host built from a fixture that declares NONE of them must neither advertise nor serve any.
    var r = await engawa.checkComposition();
    PER_APP.forEach(function (ns) {
      assert(r.capabilities.indexOf(ns) < 0, 'undeclared per-app namespace absent from capabilities: ' + ns);
    });
    // §1.1: invoking an uncomposed namespace rejects locally with ENOTSUP (clipboard is representative).
    assertEqual(r.clipboardCode, 'ENOTSUP', 'invoking an uncomposed namespace rejects locally with ENOTSUP');

    // The mandatory core is composed regardless of declaration.
    CORE.forEach(function (ns) {
      assert(r.capabilities.indexOf(ns) >= 0, 'mandatory core namespace present without declaration: ' + ns);
    });
  });
})();
