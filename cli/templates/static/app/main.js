'use strict';
// A static shell: no host commands are invoked. The only thing this touches is the two
// synchronous properties the shell exposes on window.engawa — engawa.platform and
// engawa.contractVersion — to prove the runtime is wired up. External script only (inline
// script is dead under the default CSP, §7.3).
(function () {
  var engawa = window.engawa;
  var footer = document.getElementById('footer');
  footer.textContent = engawa.platform + ' · contract ' + engawa.contractVersion;
})();
