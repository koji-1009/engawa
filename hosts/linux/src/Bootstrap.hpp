#pragma once
// Builds the document-start script injected via WebKitGTK's user-script path
// (webkit_user_content_manager_add_script) — the "evaluate a string" companion to postMessage. That
// path runs before any page script and is NOT subject to the page CSP (contract §7.3), which is what
// §1/§6 require: the bridge is installed even under `default-src app:; script-src app:`.
//
// The user script is injected into EVERY frame, so the §6 guard lives inside the script:
//   • top-level app:// document  → real __shell (handshake + postMessage) then shell.js  → engawa
//   • top-level non-app document → a DEAD __shell (no-op postMessage, empty capabilities) → no engawa
//   • any subframe               → nothing at all (iframes never receive __shell, §6)
#include <optional>
#include <string>
#include <vector>

namespace Bootstrap {

std::string build(const std::string& platform, const std::string& contractVersion,
                  const std::vector<std::string>& capabilities, const std::string& shellJs,
                  bool conformance, const std::optional<std::string>& autotestJson);

// Read by the `introspect` control message — mirrors the live in-page runtime's read-only surface
// (§1.1) plus the CSP probes the driver's index.html plants (inline blocked, external ran).
inline const char* const IntrospectScript = R"JS((function(){
    return {
      platform: engawa.platform,
      contractVersion: engawa.contractVersion,
      capabilities: engawa.capabilities,
      frozen: Object.isFrozen(engawa),
      inlineScriptBlocked: (typeof window.__inlineRan === 'undefined'),
      externalScriptRan: (window.__externalRan === true)
    };
  })())JS";

// Read by `frameCheck` — the driver's index.html embeds an app:// iframe whose frame.js records onto
// the parent whether it saw __shell (it must not, §6).
inline const char* const FrameCheckScript = R"JS((function(){
    return {
      iframeLoaded: !!window.__iframeLoaded,
      iframeHadShell: !!window.__iframeHadShell,
      topHasShell: (typeof window.__shell !== 'undefined')
    };
  })())JS";

// Read by `nonAppCheck` on an about:blank document — a dead __shell, no engawa/shell.js (§6/§7).
inline const char* const NonAppCheckScript = R"JS((function(){
    var s = window.__shell;
    var dead = !!(s && s.__dead === true && Array.isArray(s.capabilities) && s.capabilities.length === 0);
    return {
      hasEngawa: (typeof window.engawa !== 'undefined'),
      hasShell: (typeof s !== 'undefined'),
      shellDead: dead
    };
  })())JS";

}  // namespace Bootstrap
