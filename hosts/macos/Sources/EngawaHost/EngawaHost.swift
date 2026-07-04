import Cocoa
import WebKit

// The macOS reference host. Implements the two protocol primitives — receive a
// string (`engawa` message handler) and evaluate a string (`__shell._deliver`) —
// injects __shell + shell.js at document start (contract §1, §6), and dispatches
// commands. For the vertical slice the only command is `echo`; stage 3 introduces
// the Adapter protocol and re-lands echo as an adapter with no privileged path.
final class EngawaHost: NSObject {
    let mode: String                 // "app" | "conformance"
    let capabilities: [String]
    let shellJS: String
    let assetRoot: URL?

    private var window: NSWindow?
    private var webView: WKWebView!
    private let schemeHandler: AppSchemeHandler

    // Outbound delivery queue (contract §2.1): one eval per main-loop tick, frames batched.
    private var outbound: [[String: Any]] = []
    private var flushScheduled = false

    init(mode: String, env: [String: String]) {
        self.mode = mode
        self.capabilities = env["ENGAWA_APP_CAPS"].map { $0.split(separator: ",").map(String.init) } ?? ["echo"]
        guard let shellPath = env["ENGAWA_SHELL_JS"],
              let src = try? String(contentsOfFile: shellPath, encoding: .utf8) else {
            Out.err("engawa: ENGAWA_SHELL_JS unset or unreadable")
            exit(3)
        }
        self.shellJS = src
        self.assetRoot = env["ENGAWA_APP_ROOT"].map { URL(fileURLWithPath: $0, isDirectory: true) }
        self.schemeHandler = AppSchemeHandler(assetRoot: assetRoot)
        super.init()
    }

    func boot() {
        let config = WKWebViewConfiguration()
        config.setURLSchemeHandler(schemeHandler, forURLScheme: "app")

        let ucc = WKUserContentController()
        ucc.addUserScript(WKUserScript(source: bootstrapScript(),
                                       injectionTime: .atDocumentStart, forMainFrameOnly: true))
        // shell.js — identical bytes across hosts.
        ucc.addUserScript(WKUserScript(source: shellJS,
                                       injectionTime: .atDocumentStart, forMainFrameOnly: true))
        ucc.add(self, name: "engawa")
        if mode == "conformance" { ucc.add(self, name: "engawaCtl") }
        config.userContentController = ucc

        webView = WKWebView(frame: NSRect(x: 0, y: 0, width: 1024, height: 720), configuration: config)
        webView.navigationDelegate = self

        if mode != "conformance" {
            let w = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1024, height: 720),
                             styleMask: [.titled, .closable, .resizable, .miniaturizable],
                             backing: .buffered, defer: false)
            w.center()
            w.title = "Engawa"
            w.contentView = webView
            w.makeKeyAndOrderFront(nil)
            window = w
        }
        webView.load(URLRequest(url: URL(string: "app://app/index.html")!))
    }

    // Bootstrap: define __shell before shell.js. Real primitives on app:// top-level
    // documents; a dead __shell (postMessage no-ops) on any other origin (contract §7).
    private func bootstrapScript() -> String {
        let caps = JSON.string(capabilities) ?? "[]"
        return """
        (function(){
          if (location.protocol === 'app:') {
            window.__shell = {
              contractVersion: "1.0",
              platform: "macos",
              capabilities: \(caps),
              postMessage: function(s){ window.webkit.messageHandlers.engawa.postMessage(s); }
            };
          } else {
            window.__shell = { contractVersion: "1.0", platform: "macos", capabilities: [], postMessage: function(){} };
          }
        })();
        """
    }

    // MARK: delivery (§2.1)

    func enqueue(_ frame: [String: Any]) {
        outbound.append(frame)
        if flushScheduled { return }
        flushScheduled = true
        DispatchQueue.main.async { [weak self] in self?.flush() }
    }

    private func flush() {
        flushScheduled = false
        if outbound.isEmpty { return }
        let batch = outbound
        outbound.removeAll()
        guard let json = JSON.string(batch) else { return }
        webView.evaluateJavaScript("__shell._deliver(\(JSON.jsStringLiteral(json)));", completionHandler: nil)
    }

    // MARK: dispatch

    private func handleRequest(_ text: String) {
        guard let obj = JSON.parse(text) as? [String: Any],
              (obj["t"] as? String) == "req",
              let id = obj["id"] as? Int,
              let cmd = obj["cmd"] as? String else { return }
        let args = obj["args"]

        switch cmd {
        case "echo":
            enqueue(["t": "res", "id": id, "ok": true, "value": args ?? NSNull()])
        default:
            enqueue(["t": "res", "id": id, "ok": false,
                     "err": ["code": "ENOSYS", "message": "unknown command: \(cmd)"]])
        }
    }

    // MARK: conformance control bridge (mode == "conformance")

    private func conformanceReady() {
        Out.line(["ctl": "ready", "platform": "macos"])
        startStdinReader()
    }

    private func startStdinReader() {
        let thread = Thread {
            while let raw = readLine(strippingNewline: true) {
                let line = raw.trimmingCharacters(in: .whitespaces)
                if line.isEmpty { continue }
                guard let ctl = JSON.parse(line) as? [String: Any],
                      let kind = ctl["ctl"] as? String else { continue }
                switch kind {
                case "invoke":
                    let reqId = ctl["reqId"] as? Int ?? -1
                    let cmd = ctl["cmd"] as? String ?? ""
                    let args = ctl["args"]
                    DispatchQueue.main.async { [weak self] in self?.runInvoke(reqId: reqId, cmd: cmd, args: args) }
                case "spike":
                    let reqId = ctl["reqId"] as? Int ?? -1
                    DispatchQueue.main.async { [weak self] in self?.runSpike(reqId: reqId) }
                case "quit":
                    exit(0)
                default:
                    break
                }
            }
            // stdin closed
            exit(0)
        }
        thread.stackSize = 1 << 20
        thread.start()
    }

    // Drive the real in-page runtime: the Node suite calls this proxy, the page's
    // real shell.js performs the round-trip, the result is posted back via engawaCtl.
    private func runInvoke(reqId: Int, cmd: String, args: Any?) {
        let cmdLit = JSON.jsStringLiteral(cmd)
        let argsJSON = JSON.string(args ?? NSNull()) ?? "null"
        let js = """
        (function(){
          try {
            Promise.resolve(engawa.invoke(\(cmdLit), \(argsJSON))).then(
              function(v){ window.webkit.messageHandlers.engawaCtl.postMessage({ctl:'result', reqId:\(reqId), ok:true, value:(v===undefined?null:v)}); },
              function(e){ window.webkit.messageHandlers.engawaCtl.postMessage({ctl:'result', reqId:\(reqId), ok:false, err:{code:(e&&e.code)||'EUNKNOWN', message:String((e&&e.message)||e)}}); }
            );
          } catch(err){
            window.webkit.messageHandlers.engawaCtl.postMessage({ctl:'result', reqId:\(reqId), ok:false, err:{code:'ETHROW', message:String(err)}});
          }
        })();
        """
        webView.evaluateJavaScript(js, completionHandler: nil)
    }

    // §5a spike: does a fetch() PUT body reach the scheme handler on this engine?
    // Runs in-page (real fetch → real WKURLSchemeHandler) and reports the handler's
    // findings back through the control channel.
    private func runSpike(reqId: Int) {
        let js = """
        (function(){
          var payload = new TextEncoder().encode('えんがわ-🌇-PUT-body-\\u2028-tail');
          fetch('app://io/spike', { method:'PUT', body: payload })
            .then(function(r){ return r.json(); })
            .then(function(j){ window.webkit.messageHandlers.engawaCtl.postMessage({ctl:'result', reqId:\(reqId), ok:true, value:j, sentLen:payload.length}); })
            .catch(function(e){ window.webkit.messageHandlers.engawaCtl.postMessage({ctl:'result', reqId:\(reqId), ok:false, err:{code:'EFETCH', message:String((e&&e.message)||e)}}); });
        })();
        """
        webView.evaluateJavaScript(js, completionHandler: nil)
    }
}

extension EngawaHost: WKScriptMessageHandler {
    func userContentController(_ ucc: WKUserContentController, didReceive message: WKScriptMessage) {
        switch message.name {
        case "engawa":
            if let s = message.body as? String { handleRequest(s) }
        case "engawaCtl":
            if let body = message.body as? [String: Any] { Out.line(body) }
        default:
            break
        }
    }
}

extension EngawaHost: WKNavigationDelegate {
    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        if mode == "conformance" { conformanceReady() }
    }
    func webView(_ webView: WKWebView, didFail navigation: WKNavigation!, withError error: Error) {
        Out.err("navigation failed: \(error.localizedDescription)")
    }
    func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
        Out.err("provisional navigation failed: \(error.localizedDescription)")
    }
}
