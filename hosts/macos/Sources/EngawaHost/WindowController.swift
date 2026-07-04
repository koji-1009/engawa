import Cocoa
import EngawaKit

// Owns the NSWindow and the close protocol (contract §4.2). By default a user close closes
// the window. An app that must intervene (e.g. unsaved changes) opts in with
// window.setCloseHandler(true); thereafter a close attempt emits window.closeRequested with a
// token and waits — the app answers via window.respondToClose(token, allow). No timeout (§4.2).
//
// AppKit is main-thread only, so the whole controller is main-actor isolated; the adapter
// awaits into it and NSWindowDelegate callbacks already arrive on main.
@MainActor
final class WindowController: NSObject, NSWindowDelegate {
    private let emitter: EventEmitter
    private weak var window: NSWindow?
    private var pendingCloseTokens: Set<Int> = []
    private var tokenSeq = 0
    private var emittedFocusEvents = false
    private var interceptClose = false   // §4.2: false → close on the button; true → defer to the app

    init(emitter: EventEmitter) {
        self.emitter = emitter
    }

    func attach(_ window: NSWindow) {
        self.window = window
        window.delegate = self
    }

    // MARK: NSWindowDelegate

    func windowShouldClose(_ sender: NSWindow) -> Bool {
        guard interceptClose else { return true }   // §4.2 default: just close
        beginClose()
        return false   // opted in: defer to the app; close only on respondToClose(allow: true)
    }

    func windowDidBecomeKey(_ notification: Notification) {
        emitter.emit("window.focus", .null)
    }

    func windowDidResignKey(_ notification: Notification) {
        emitter.emit("window.blur", .null)
    }

    func windowDidResize(_ notification: Notification) {
        let s = window?.contentView?.bounds.size ?? .zero
        emitter.emit("window.resize", .object(["width": .number(Double(s.width)), "height": .number(Double(s.height))]))
    }

    // Conformance hook: fire many resizes in one tick so the suite can observe §2.1 coalescing.
    func resizeStorm(from: Double, count: Int) {
        for i in 0..<count { window?.setContentSize(NSSize(width: from + Double(i), height: from + Double(i))) }
    }

    // MARK: commands

    func setCloseHandler(_ enabled: Bool) { interceptClose = enabled }

    @discardableResult
    func beginClose() -> Int {
        tokenSeq += 1
        let token = tokenSeq
        pendingCloseTokens.insert(token)
        emitter.emit("window.closeRequested", .object(["token": .number(Double(token))]))
        return token
    }

    // Conformance hook: simulate a user close attempt through the §4.2 gate. Returns whether it
    // was deferred (intercepted) rather than closed; never destroys the off-screen suite window.
    func requestCloseForTest() -> Bool {
        guard interceptClose else { return false }
        beginClose()
        return true
    }

    func setTitle(_ title: String) { window?.title = title }

    func size() -> JSONValue {
        let s = window?.contentView?.bounds.size ?? .zero
        return .object(["width": .number(Double(s.width)), "height": .number(Double(s.height))])
    }

    func setSize(width: Double, height: Double) {
        window?.setContentSize(NSSize(width: width, height: height))
    }

    func setResizable(_ resizable: Bool) {
        guard let w = window else { return }
        if resizable { w.styleMask.insert(.resizable) } else { w.styleMask.remove(.resizable) }
    }

    func minimize() { window?.miniaturize(nil) }
    func maximize() { window?.zoom(nil) }
    func close() { window?.close() }

    func respondToClose(token: Int, allow: Bool) throws {
        guard pendingCloseTokens.contains(token) else {
            throw AdapterError("EINVAL", "unknown or consumed close token: \(token)")
        }
        pendingCloseTokens.remove(token)
        if allow { window?.close() }
    }
}
