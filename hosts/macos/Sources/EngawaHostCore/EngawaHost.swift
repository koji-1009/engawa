import Cocoa
import WebKit
import EngawaKit
import EngawaUpdate

// The macOS reference host. Implements the two protocol primitives — receive a
// string (`engawa` message handler) and evaluate a string (`__shell._deliver`) —
// injects __shell + shell.js at document start (contract §1, §6), and routes every
// command through the adapter registry (§3). Built-in namespaces are adapters; there
// is no privileged dispatch path. The only registered adapter so far is `echo`.
//
// Main-actor isolated: it drives WebKit (main-thread only) and owns the outbound queue.
// Adapters run off-actor (their handle is nonisolated async); results hop back here.
@MainActor
final class EngawaHost: NSObject {
    static let contractVersion = "0.1.0"   // DRAFT — pre-1.0 semver until the contract is frozen
    static let hostVersion = "macos-host-0.1.0"
    static let engineFloor = "605.1.15"    // WebKit floor (§9); modern WebKit far exceeds it

    // Detected engine version, or the ENGAWA_FAKE_ENGINE_VERSION substitute (§9 testability hook).
    static func engineVersion(env: [String: String]) -> String {
        if let fake = env["ENGAWA_FAKE_ENGINE_VERSION"] { return fake }
        return (Bundle(identifier: "com.apple.WebKit")?.object(forInfoDictionaryKey: "CFBundleVersion") as? String) ?? "99999"
    }

    static func meetsEngineFloor(_ version: String) -> Bool {
        func parts(_ s: String) -> [Int] { s.split(separator: ".").map { Int($0) ?? 0 } }
        let v = parts(version), floor = parts(engineFloor)
        for i in 0..<max(v.count, floor.count) {
            let a = i < v.count ? v[i] : 0, b = i < floor.count ? floor[i] : 0
            if a != b { return a > b }
        }
        return true
    }

    let mode: String                 // "app" | "conformance"
    private(set) var capabilities: [String] = []
    let shellJS: String
    let dirs: AppDirs
    let appVersion: String
    let engineVersion: String
    let manifest: Manifest?
    let slots: SlotManager
    let ioTokens = IoTokenStore()   // §5a binary I/O tokens, shared with the scheme handler
    let autotest: Bool
    private let autotestUpdate: String   // ENGAWA_AUTOTEST_UPDATE JSON, or "null"
    private let wipeStorage: Bool        // §10: wipe WebView storage at boot, then boot anyway
    private let startURL: URL            // usually app://app/index.html; overridable for §6 tests
    private let appAdapters: [any Adapter]   // the app's statically-composed adapters (§3)

    private var window: NSWindow?
    private var webView: WKWebView!
    private let schemeHandler: AppSchemeHandler
    private var router: Router!
    private var windowController: WindowController!

    // Outbound delivery queue (contract §2.1): one eval per main-loop tick, frames batched.
    private var outbound: [[String: Any]] = []
    private var flushScheduled = false

    private var hostEmitter: EventEmitter?          // for app-mode event emission (e.g. renderCrashed)
    private var stdinReaderStarted = false          // reload re-fires didFinish; start the reader once
    private var crashCount = 0
    private var crashTimes: [Date] = []             // §10: three crashes in 60 s → error screen

    init(mode: String, env: [String: String], appAdapters: [any Adapter] = []) {
        self.mode = mode
        self.appAdapters = appAdapters

        // Assets resolve from env (conformance) or, failing that, the .app bundle (a real app).
        let bundleResources = Bundle.main.resourceURL

        // shell.js: ENGAWA_SHELL_JS, else <bundle>/Resources/shell.js.
        let shellSrc: String? = {
            if let p = env["ENGAWA_SHELL_JS"], let s = try? String(contentsOfFile: p, encoding: .utf8) { return s }
            if let u = bundleResources?.appendingPathComponent("shell.js"),
               let s = try? String(contentsOf: u, encoding: .utf8) { return s }
            return nil
        }()
        guard let shellSrc = shellSrc else {
            Out.err("engawa: shell.js not found (ENGAWA_SHELL_JS or bundle Resources/shell.js)")
            exit(3)
        }
        self.shellJS = shellSrc
        self.dirs = AppDirs.resolve(env: env)
        self.appVersion = env["ENGAWA_APP_VERSION"] ?? (Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String) ?? "0.0.0"
        self.engineVersion = Self.engineVersion(env: env)
        self.manifest = Manifest.load(env: env)

        // App assets seed: ENGAWA_APP_ROOT, else <bundle>/Resources/app.
        let seed = env["ENGAWA_APP_ROOT"].map { URL(fileURLWithPath: $0, isDirectory: true) }
            ?? bundleResources?.appendingPathComponent("app")

        // Trust root (§7.1): ENGAWA_TRUST_ROOT, else <bundle>/Resources/trust-root.txt.
        let trustRoot: String? = env["ENGAWA_TRUST_ROOT"] ?? {
            guard let u = bundleResources?.appendingPathComponent("trust-root.txt"),
                  let s = try? String(contentsOf: u, encoding: .utf8) else { return nil }
            return s.trimmingCharacters(in: .whitespacesAndNewlines)
        }()

        // Slots live under the real per-app data dir unless an isolated root is given.
        let slotsBase = URL(fileURLWithPath: env["ENGAWA_DATA_ROOT"] ?? dirs.appData)
            .appendingPathComponent("engawa")
        let slots = SlotManager(base: slotsBase, seed: seed, trustRootB64: trustRoot)
        self.slots = slots
        self.schemeHandler = AppSchemeHandler(rootProvider: { slots.liveSlotDir() }, ioTokens: ioTokens)
        self.autotest = env["ENGAWA_AUTOTEST"] == "1"
        self.autotestUpdate = env["ENGAWA_AUTOTEST_UPDATE"] ?? "null"
        self.wipeStorage = env["ENGAWA_WIPE_STORAGE"] == "1"
        self.startURL = URL(string: env["ENGAWA_START_URL"] ?? "app://app/index.html") ?? URL(string: "app://app/index.html")!
        super.init()
    }

    // Register the in-tree adapters and derive capabilities from them (§3). Built-ins go
    // here alongside any app adapters; the router treats them identically.
    private func registerAdapters() {
        // The emitter may be called from an adapter's background thread; hop to the main actor.
        let emitter = HostEmitter { [weak self] topic, payload in
            Task { @MainActor in self?.emitEvent(topic: topic, payload: payload) }
        }
        router = Router(emitter: emitter)
        hostEmitter = emitter
        router.register(EchoAdapter())
        router.register(PathAdapter(dirs: dirs))
        router.register(FsAdapter(ioTokens: ioTokens))
        router.register(AppAdapter(appVersion: appVersion,
                                   hostVersion: Self.hostVersion,
                                   contractVersion: Self.contractVersion,
                                   engineVersion: engineVersion,
                                   autotest: autotest))
        // A private pasteboard under conformance so the suite never touches the user's clipboard.
        let pasteboard = mode == "conformance"
            ? NSPasteboard(name: NSPasteboard.Name("dev.engawa.conformance"))
            : NSPasteboard.general
        router.register(ClipboardAdapter(pasteboard: pasteboard))
        windowController = WindowController(emitter: emitter)
        router.register(WindowAdapter(controller: windowController, conformance: mode == "conformance"))
        router.register(ShellOpenAdapter(conformance: mode == "conformance"))
        router.register(NotificationAdapter(conformance: mode == "conformance"))
        router.register(ProcessAdapter(manifest: manifest, emitter: emitter))
        router.register(DialogAdapter(conformance: mode == "conformance"))
        router.register(UpdateAdapter(host: slots)) // contract-coupled adapter (adapters/update), always present
        // The app's statically-composed adapters (e.g. sqlite) — only what this app declared (§3).
        for adapter in appAdapters { router.register(adapter) }
        capabilities = router.namespaces
    }

    func boot() {
        registerAdapters()

        // §9 engine floor: below the spec minimum → spec'd error screen, no partial boot.
        guard Self.meetsEngineFloor(engineVersion) else {
            rejectEngineFloor()
            return
        }

        _ = slots.boot()   // initialize/seed slots and decide the live slot before loading

        let config = WKWebViewConfiguration()
        config.setURLSchemeHandler(schemeHandler, forURLScheme: "app")

        let ucc = WKUserContentController()
        // One document-start, main-frame-only script: bootstrap that runs shell.js ONLY on
        // app:// top-level documents (§6). Non-app origins get a dead __shell and no shell.js.
        ucc.addUserScript(WKUserScript(source: bootstrapScript(),
                                       injectionTime: .atDocumentStart, forMainFrameOnly: true))
        ucc.add(self, name: "engawa")
        if mode == "conformance" { ucc.add(self, name: "engawaCtl") }
        config.userContentController = ucc

        webView = WKWebView(frame: NSRect(x: 0, y: 0, width: 1024, height: 720), configuration: config)
        webView.navigationDelegate = self

        // Always back the app with a real NSWindow so the `window` namespace operates on a
        // genuine window in both modes. Under conformance it stays off-screen and unshown.
        let w = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1024, height: 720),
                         styleMask: [.titled, .closable, .resizable, .miniaturizable],
                         backing: .buffered, defer: false)
        w.title = "Engawa"
        w.contentView = webView
        window = w
        windowController.attach(w)
        if mode == "conformance" || autotest {
            w.setFrameOrigin(NSPoint(x: -10000, y: -10000))   // off-screen; never shown
        } else {
            w.center()
            w.makeKeyAndOrderFront(nil)
        }
        // §10: WebView-managed storage is cache. Wiping it must not stop the app from booting —
        // durable data lives in fs/sqlite, not IndexedDB/localStorage/caches.
        let start = URLRequest(url: startURL)
        if wipeStorage {
            WKWebsiteDataStore.default().removeData(
                ofTypes: WKWebsiteDataStore.allWebsiteDataTypes(),
                modifiedSince: Date(timeIntervalSince1970: 0)
            ) { [weak self] in self?.webView.load(start) }
        } else {
            webView.load(start)
        }
    }

    // §9: engine below the floor. No app, no __shell — a spec'd error screen (or, under
    // conformance, a report + exit so the suite can assert floor rejection on any machine).
    private func rejectEngineFloor() {
        if mode == "conformance" {
            Out.line(["ctl": "floorRejected", "detected": engineVersion, "required": Self.engineFloor])
            exit(0)
        }
        let html = "<!doctype html><meta charset=\"utf-8\"><title>Unsupported</title>"
            + "<h1>Unsupported engine</h1><p>Detected \(engineVersion); requires at least \(Self.engineFloor).</p>"
        let w = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 520, height: 220),
                         styleMask: [.titled, .closable], backing: .buffered, defer: false)
        let errorView = WKWebView(frame: w.contentLayoutRect)
        errorView.loadHTMLString(html, baseURL: nil)
        w.contentView = errorView
        w.title = "Engawa"
        w.center()
        w.makeKeyAndOrderFront(nil)
        window = w
    }

    // Bootstrap (§1, §6, §7): on a top-level app:// document, define the real __shell and then
    // run shell.js (identical bytes, embedded here) so the app gets `engawa`. On any other origin
    // (http(s), about:blank) define a DEAD __shell (postMessage no-ops) and do NOT run shell.js —
    // no `engawa`, no bridge to commands.
    private func bootstrapScript() -> String {
        let caps = JSON.string(capabilities) ?? "[]"
        let cv = Self.contractVersion
        let autotestJS = autotest ? "window.__engawaAutotest = { update: \(autotestUpdate) };" : ""
        return """
        (function(){
          if (location.protocol !== 'app:') {
            window.__shell = { contractVersion: "\(cv)", platform: "macos", capabilities: [], postMessage: function(){} };
            return;   // §6: no shell.js on non-app documents
          }
          window.__shell = {
            contractVersion: "\(cv)",
            platform: "macos",
            capabilities: \(caps),
            postMessage: function(s){ window.webkit.messageHandlers.engawa.postMessage(s); }
          };
          \(autotestJS)
          // shell.js — identical bytes across hosts; runs only on app:// (reached only here).
          \(shellJS)
        })();
        """
    }

    // MARK: delivery (§2.1)

    func enqueue(_ frame: [String: Any]) {
        outbound.append(frame)
        if flushScheduled { return }
        flushScheduled = true
        // One eval per main-loop tick: schedule flush on the next runloop iteration.
        DispatchQueue.main.async { [weak self] in MainActor.assumeIsolated { self?.flush() } }
    }

    private func flush() {
        flushScheduled = false
        if outbound.isEmpty { return }
        let batch = Self.coalesceResize(outbound)   // §2.1: latest window.resize per batch only
        outbound.removeAll()
        guard let json = JSON.string(batch) else { return }
        webView.evaluateJavaScript("__shell._deliver(\(JSON.jsStringLiteral(json)));", completionHandler: nil)
    }

    // §2.1: coalesce high-frequency window.resize events — keep only the latest in a batch.
    private static func coalesceResize(_ frames: [[String: Any]]) -> [[String: Any]] {
        func isResize(_ f: [String: Any]) -> Bool {
            (f["t"] as? String) == "evt" && (f["topic"] as? String) == "window.resize"
        }
        var lastResize = -1
        for (i, f) in frames.enumerated() where isResize(f) { lastResize = i }
        if lastResize < 0 { return frames }
        return frames.enumerated().compactMap { i, f in (isResize(f) && i != lastResize) ? nil : f }
    }

    // MARK: dispatch

    private func handleRequest(_ text: String) {
        guard let obj = JSON.parse(text) as? [String: Any],
              (obj["t"] as? String) == "req",
              let id = obj["id"] as? Int,
              let cmd = obj["cmd"] as? String else { return }
        let args = JSONValue.from(obj["args"])
        let (namespace, command) = splitCommand(cmd)

        guard let adapter = router.adapter(for: namespace) else {
            // shell.js gates unserved namespaces locally (§1.1); reaching here means the
            // page bypassed the runtime, so answer defensively rather than trust it.
            enqueue(errFrame(id, "ENOTSUP", "namespace not served: \(namespace)"))
            return
        }

        // Task inherits the main actor; adapter.handle is nonisolated async, so it runs
        // off-actor and the result resumes here on main to enqueue.
        Task {
            do {
                let value = try await adapter.handle(command, args)
                enqueue(okFrame(id, value))
            } catch let e as AdapterError {
                enqueue(errFrame(id, e.code, e.message))
            } catch {
                enqueue(errFrame(id, "EUNKNOWN", "\(error)"))
            }
        }
    }

    private func splitCommand(_ cmd: String) -> (namespace: String, command: String) {
        if let dot = cmd.firstIndex(of: ".") {
            return (String(cmd[..<dot]), String(cmd[cmd.index(after: dot)...]))
        }
        return (cmd, "")   // a bare command (e.g. the echo fixture) is namespace-only
    }

    private func okFrame(_ id: Int, _ value: JSONValue) -> [String: Any] {
        ["t": "res", "id": id, "ok": true, "value": value.toFoundation()]
    }

    private func errFrame(_ id: Int, _ code: String, _ message: String) -> [String: Any] {
        ["t": "res", "id": id, "ok": false, "err": ["code": code, "message": message]]
    }

    // An adapter emitted an event (§2, §2.1). Already on the main actor (see the emitter sink).
    private func emitEvent(topic: String, payload: JSONValue) {
        enqueue(["t": "evt", "topic": topic, "payload": payload.toFoundation()])
    }

    // MARK: conformance control bridge (mode == "conformance")

    private func conformanceReady() {
        // didFinish re-fires on reload (e.g. crash recovery); start the stdin reader only once.
        if stdinReaderStarted { return }
        stdinReaderStarted = true
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
                    // Serialize args here (Sendable String) rather than capture a non-Sendable Any.
                    let argsJSON = JSON.string(ctl["args"] ?? NSNull()) ?? "null"
                    Task { @MainActor [weak self] in self?.runInvoke(reqId: reqId, cmd: cmd, argsJSON: argsJSON) }
                case "ioPut":
                    let reqId = ctl["reqId"] as? Int ?? -1
                    let url = ctl["url"] as? String ?? ""
                    let dataB64 = ctl["dataB64"] as? String ?? ""
                    Task { @MainActor [weak self] in self?.runIoPut(reqId: reqId, url: url, dataB64: dataB64) }
                case "ioGet":
                    let reqId = ctl["reqId"] as? Int ?? -1
                    let url = ctl["url"] as? String ?? ""
                    Task { @MainActor [weak self] in self?.runIoGet(reqId: reqId, url: url) }
                case "introspect":
                    let reqId = ctl["reqId"] as? Int ?? -1
                    Task { @MainActor [weak self] in self?.runIntrospect(reqId: reqId) }
                case "frameCheck":
                    let reqId = ctl["reqId"] as? Int ?? -1
                    Task { @MainActor [weak self] in self?.runFrameCheck(reqId: reqId) }
                case "nonAppCheck":
                    let reqId = ctl["reqId"] as? Int ?? -1
                    Task { @MainActor [weak self] in self?.runNonAppCheck(reqId: reqId) }
                case "subscribe":
                    if let topic = ctl["topic"] as? String {
                        Task { @MainActor [weak self] in self?.runSubscribe(topic: topic) }
                    }
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
    private func runInvoke(reqId: Int, cmd: String, argsJSON: String) {
        let cmdLit = JSON.jsStringLiteral(cmd)
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

    // Report the real in-page runtime's read-only surface (contract §1.1) so the driver's
    // proxy can mirror it faithfully — property checks then test the live engawa, not the proxy.
    private func runIntrospect(reqId: Int) {
        let js = """
        (function(){
          window.webkit.messageHandlers.engawaCtl.postMessage({ctl:'result', reqId:\(reqId), ok:true, value:{
            frozen: Object.isFrozen(engawa),
            capabilities: engawa.capabilities,
            platform: engawa.platform,
            contractVersion: engawa.contractVersion,
            inlineScriptBlocked: (typeof window.__inlineRan === 'undefined'),
            externalScriptRan: (window.__externalRan === true)
          }});
        })();
        """
        webView.evaluateJavaScript(js, completionHandler: nil)
    }

    // Subscribe the in-page runtime to a topic and relay each event out over the control
    // channel, so the runner (which drives via the proxy) can observe real event delivery.
    private func runSubscribe(topic: String) {
        let topicLit = JSON.jsStringLiteral(topic)
        let js = """
        (function(){
          engawa.on(\(topicLit), function(payload){
            window.webkit.messageHandlers.engawaCtl.postMessage({ctl:'event', topic:\(topicLit), payload:(payload===undefined?null:payload)});
          });
        })();
        """
        webView.evaluateJavaScript(js, completionHandler: nil)
    }

    // §6/§7: on a non-app document, report the injection state — a dead __shell (no-op
    // postMessage, empty capabilities) and no `engawa`/shell.js.
    private func runNonAppCheck(reqId: Int) {
        let js = """
        (function(){
          window.webkit.messageHandlers.engawaCtl.postMessage({ctl:'result', reqId:\(reqId), ok:true, value:{
            protocol: location.protocol,
            hasShell: (typeof window.__shell !== 'undefined'),
            shellDead: (typeof window.__shell !== 'undefined' && !!window.__shell.capabilities && window.__shell.capabilities.length === 0),
            hasEngawa: (typeof window.engawa !== 'undefined')
          }});
        })();
        """
        webView.evaluateJavaScript(js, completionHandler: nil)
    }

    // §6 injection matrix: report whether the app:// iframe received __shell (it must not).
    private func runFrameCheck(reqId: Int) {
        let js = """
        (function(){
          window.webkit.messageHandlers.engawaCtl.postMessage({ctl:'result', reqId:\(reqId), ok:true, value:{
            iframeLoaded: window.__iframeLoaded === true,
            iframeHadShell: window.__iframeHadShell === true,
            topHasShell: (typeof window.__shell !== 'undefined')
          }});
        })();
        """
        webView.evaluateJavaScript(js, completionHandler: nil)
    }

    // §5a binary I/O is an in-page fetch to app://io; the runner drives via the proxy, so the
    // host performs the fetch in-page and relays the outcome. Data crosses the control channel
    // base64-encoded (the control channel is not the message channel).
    private func runIoPut(reqId: Int, url: String, dataB64: String) {
        let js = """
        (function(){
          var bin = atob(\(JSON.jsStringLiteral(dataB64)));
          var bytes = new Uint8Array(bin.length);
          for (var i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
          fetch(\(JSON.jsStringLiteral(url)), { method:'PUT', body: bytes })
            .then(function(r){ return r.json(); })
            .then(function(j){ window.webkit.messageHandlers.engawaCtl.postMessage({ctl:'result', reqId:\(reqId), ok:true, value:j}); })
            .catch(function(e){ window.webkit.messageHandlers.engawaCtl.postMessage({ctl:'result', reqId:\(reqId), ok:false, err:{code:'EFETCH', message:String((e&&e.message)||e)}}); });
        })();
        """
        webView.evaluateJavaScript(js, completionHandler: nil)
    }

    private func runIoGet(reqId: Int, url: String) {
        let js = """
        (function(){
          fetch(\(JSON.jsStringLiteral(url)))
            .then(function(r){ return r.arrayBuffer(); })
            .then(function(buf){
              var bytes = new Uint8Array(buf), bin = '', CH = 0x8000;
              for (var i = 0; i < bytes.length; i += CH) bin += String.fromCharCode.apply(null, bytes.subarray(i, i + CH));
              window.webkit.messageHandlers.engawaCtl.postMessage({ctl:'result', reqId:\(reqId), ok:true, value:{ base64: btoa(bin), len: bytes.length }});
            })
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

    // §10 renderer crash recovery: reload on renderer death and signal app.renderCrashed with a
    // crash counter. Three crashes in 60 s → spec'd error screen instead of a reload loop.
    // (Fires on real renderer crashes; a synthetic trigger for the offscreen conformance host
    // isn't reliable on this engine — WebKit does not fire this for _killWebContentProcess when
    // the renderer is suspended — so this recovery path is not yet exercised by the suite.)
    func webViewWebContentProcessDidTerminate(_ webView: WKWebView) {
        let now = Date()
        crashTimes.append(now)
        crashTimes = crashTimes.filter { now.timeIntervalSince($0) < 60 }
        crashCount += 1

        // Emit the signal. In conformance the crash destroyed the in-page subscription, so
        // report on the control channel directly; in app mode use the normal event path.
        if mode == "conformance" {
            Out.line(["ctl": "event", "topic": "app.renderCrashed", "payload": ["count": crashCount]])
        } else {
            hostEmitter?.emit("app.renderCrashed", .object(["count": .number(Double(crashCount))]))
        }

        if crashTimes.count >= 3 {
            webView.loadHTMLString("<!doctype html><meta charset=\"utf-8\"><title>Engawa</title><h1>The app keeps crashing</h1>", baseURL: nil)
        } else {
            webView.reload()
        }
    }
    func webView(_ webView: WKWebView, didFail navigation: WKNavigation!, withError error: Error) {
        Out.err("navigation failed: \(error.localizedDescription)")
    }
    func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
        Out.err("provisional navigation failed: \(error.localizedDescription)")
    }
}
