'use strict';
// A minimal app that reports the self-managed update state on load. update.status returns the
// current A/B slot picture (adapters/update/spec.md): { currentSlot, bootingSlot, version,
// hasPending, pendingSlot }. Trust (§7.1) and the atomic slot swap (§8) are host obligations.
// External script only — inline script is dead under the default CSP (§7.3).
(function () {
  var engawa = window.engawa;

  function set(id, value) { document.getElementById(id).textContent = value; }

  async function refresh() {
    try {
      var s = await engawa.invoke('update.status');
      set('currentSlot', s.currentSlot);
      set('version', s.version);
      set('hasPending', s.hasPending ? 'yes (slot ' + s.pendingSlot + ')' : 'no');
    } catch (e) {
      var err = document.getElementById('error');
      err.hidden = false;
      err.textContent = 'update.status failed: ' + ((e && e.message) || String(e));
    }
  }

  refresh();
})();
