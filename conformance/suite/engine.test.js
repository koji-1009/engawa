'use strict';
// Engine floor (contract §9, §11): below the spec minimum → spec'd error screen, no partial
// boot. ENGAWA_FAKE_ENGINE_VERSION substitutes the detected version so this runs on any machine.
// Requires a real host (the mock host has no engine and skips).

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert;

  test('§9 a below-floor engine is rejected (no boot); an above-floor engine boots', async function (engawa) {
    if (!engawa.checkEngineFloor) return;   // requires a real host

    // The floor is per-engine (WebKit 605.1.15 on macOS, a Chromium major on WebView2), so the
    // straddle versions are the driver's to supply — a fixed '600.0' is below WebKit's floor but
    // *above* any WebView2 floor. A driver that names no samples falls back to the WebKit values.
    var samples = engawa.engineFloorSamples || { below: '600.0', above: '99999' };

    var low = await engawa.checkEngineFloor(samples.below);   // below the host's engine floor
    assert(low.rejected === true, 'a below-floor engine must be rejected with no partial boot');
    assert(typeof low.detected === 'string' && typeof low.required === 'string', 'reports detected + required');

    var high = await engawa.checkEngineFloor(samples.above);  // above the floor
    assert(high.rejected === false && high.booted === true, 'an above-floor engine boots normally (reaches ready, not a timeout)');
  });

  test('§9 engine detection failure fails closed (rejected, not assumed above the floor)', async function (engawa) {
    if (!engawa.checkEngineUndetected) return;   // requires a real host + the detection-failure hook

    // When the host cannot read the engine version at all, it MUST route to the same rejection
    // (no partial boot) as a below-floor engine — never substitute a value that passes the floor.
    var r = await engawa.checkEngineUndetected();
    assert(r.rejected === true, 'undetectable engine is rejected, not booted as if above the floor');
    assert(typeof r.detected === 'string' && r.detected.length > 0, 'the rejection reports a detected version (e.g. "unknown")');
  });
})();
