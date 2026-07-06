#include "Bootstrap.hpp"

#include "Json.hpp"

namespace {

// In conformance mode the driver drives invoke()/on() through the REAL in-page runtime and needs the
// async results back. ExecuteScript cannot await a page promise, so the page reports results over the
// same chrome.webview channel, tagged `__conf` so the host can tell a control-relay message apart
// from an ordinary `t:'req'` frame. This exercises the full stack: control invoke → engawa.invoke →
// __shell.postMessage → native dispatch → _deliver → promise → report back.
const char* const kConformanceRelay = R"JS(
  var post = function(m){ window.chrome.webview.postMessage(JSON.stringify(m)); };
  window.__engawaConf = {
    invoke: function(reqId, cmd, args){
      try {
        engawa.invoke(cmd, args).then(
          function(v){ post({ __conf: 'result', reqId: reqId, ok: true, value: (v === undefined ? null : v) }); },
          function(e){ post({ __conf: 'result', reqId: reqId, ok: false, err: { code: (e && e.code) || 'EUNKNOWN', message: (e && e.message) || '' } }); }
        );
      } catch (e) { post({ __conf: 'result', reqId: reqId, ok: false, err: { code: 'EUNKNOWN', message: String(e) } }); }
    },
    subscribe: function(topic){
      engawa.on(topic, function(p){ post({ __conf: 'event', topic: topic, payload: (p === undefined ? null : p) }); });
    },
    ioPut: function(reqId, url, dataB64){
      var bin = atob(dataB64), bytes = new Uint8Array(bin.length);
      for (var i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
      fetch(url, { method: 'PUT', body: bytes })
        .then(function(r){ return r.json(); })
        .then(function(body){ post({ __conf: 'result', reqId: reqId, ok: true, value: body }); })
        .catch(function(e){ post({ __conf: 'result', reqId: reqId, ok: true, value: { ok: false, err: { code: 'EIO', message: String(e) } } }); });
    },
    ioGet: function(reqId, url){
      fetch(url).then(function(r){ return r.arrayBuffer(); }).then(function(buf){
        var bytes = new Uint8Array(buf), bin = '';
        for (var i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
        post({ __conf: 'result', reqId: reqId, ok: true, value: { base64: btoa(bin) } });
      }).catch(function(e){ post({ __conf: 'result', reqId: reqId, ok: false, err: { code: 'EIO', message: String(e) } }); });
    }
  };
)JS";

}  // namespace

namespace Bootstrap {

std::string build(const std::string& platform, const std::string& contractVersion,
                  const std::vector<std::string>& capabilities, const std::string& shellJs,
                  bool conformance, const std::optional<std::string>& autotestJson) {
    std::string caps = Json(capabilities).dump();
    std::string plat = Json(platform).dump();
    std::string ver = Json(contractVersion).dump();

    std::string s;
    s += "(function(){\n";
    s += "try{\n";
    // §6: subframes get nothing.
    s += "  if (window.top !== window) { return; }\n";
    s += "  var isApp = (location.protocol === 'app:' && location.host === 'app');\n";
    s += "  if (!isApp) {\n";
    // §7: a non-app top-level document gets a dead bridge — postMessage no-ops, no capabilities, and
    // shell.js is never run, so `engawa` is absent.
    s += "    window.__shell = { __dead: true, contractVersion: " + ver +
         ", platform: " + plat + ", capabilities: [], postMessage: function(){} };\n";
    s += "    return;\n";
    s += "  }\n";
    // §1: the real bridge. postMessage rides WebView2's host-object channel (chrome.webview), which
    // is not governed by the page CSP.
    s += "  window.__shell = {\n";
    s += "    contractVersion: " + ver + ",\n";
    s += "    platform: " + plat + ",\n";
    s += "    capabilities: " + caps + ",\n";
    s += "    postMessage: function(s){ window.chrome.webview.postMessage(s); }\n";
    s += "  };\n";
    // shell.js — identical bytes on every host (§1). Defines __shell._deliver and globalThis.engawa
    // on top of the two primitives.
    s += "(function(){\n";
    s += shellJs;
    s += "\n})();\n";

    // Autotest hook (make-notes gate): examples/notes self-drives when window.__engawaAutotest is
    // present; the update descriptor rides ENGAWA_AUTOTEST_UPDATE. Set before the app script runs.
    if (autotestJson) {
        s += "  window.__engawaAutotest = ";
        s += *autotestJson;
        s += ";\n";
    }

    if (conformance) s += kConformanceRelay;

    s += "} catch (e) { /* injection must never throw into the page */ }\n";
    s += "})();\n";
    return s;
}

}  // namespace Bootstrap
