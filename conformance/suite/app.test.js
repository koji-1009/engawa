'use strict';
// app namespace (spec/commands/app.md). version + engineInfo only; quit ends the process
// and is covered by the make-notes gate, not here.

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert, assertEqual = h.assertEqual;

  test('app.version is a non-empty string', async function (engawa) {
    var v = await engawa.invoke('app.version');
    assert(typeof v === 'string' && v.length > 0, 'version string, got ' + JSON.stringify(v));
  });

  test('app.version comes from the app manifest (engawa.json), not a build/default fallback', async function (engawa) {
    if (!engawa.manifestVersion) return;   // requires a real host serving a bundle manifest
    // ENGAWA_APP_VERSION is unset, so a correct host reports the manifest's version — not the
    // host binary's Info.plist and not the "0.0.0" last-resort default (spec/commands/app.md).
    assertEqual(await engawa.invoke('app.version'), engawa.manifestVersion, 'app.version is the manifest version');
  });

  test('app.engineInfo reports engine/host/contract', async function (engawa) {
    var info = await engawa.invoke('app.engineInfo');
    assert(info && typeof info === 'object', 'object');
    assert(typeof info.engine === 'string' && info.engine.length > 0, 'engine');
    assert(typeof info.engineVersion === 'string' && info.engineVersion.length > 0, 'engineVersion');
    assert(typeof info.hostVersion === 'string' && info.hostVersion.length > 0, 'hostVersion');
    assertEqual(info.contractVersion, '0.1.0', 'contractVersion is the running contract');
  });

  test('app.engineInfo.contractVersion matches engawa.contractVersion', async function (engawa) {
    var info = await engawa.invoke('app.engineInfo');
    assertEqual(info.contractVersion, engawa.contractVersion, 'one source of truth (§1.1)');
  });
})();
