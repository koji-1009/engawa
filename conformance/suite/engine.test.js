'use strict';
// Engine floor (contract §9, §11): below the spec minimum → spec'd error screen, no partial
// boot. ENGAWA_FAKE_ENGINE_VERSION substitutes the detected version so this runs on any machine.
// Requires a real host (the mock host has no engine and skips).

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert;

  test('§9 a below-floor engine is rejected (no boot); an above-floor engine boots', async function (engawa) {
    if (!engawa.checkEngineFloor) return;   // requires a real host

    var low = await engawa.checkEngineFloor('600.0');   // below the 605.1.15 floor
    assert(low.rejected === true, 'a below-floor engine must be rejected with no partial boot');
    assert(typeof low.detected === 'string' && typeof low.required === 'string', 'reports detected + required');

    var high = await engawa.checkEngineFloor('99999');  // above the floor
    assert(high.rejected === false, 'an above-floor engine boots normally');
  });
})();
