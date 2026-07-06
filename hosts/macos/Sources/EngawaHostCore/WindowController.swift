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
    private let conformance: Bool
    private weak var window: NSWindow?
    private var pendingCloseTokens: Set<Int> = []
    private var tokenSeq = 0
    private var emittedFocusEvents = false
    private var interceptClose = false   // §4.2: false → close on the button; true → defer to the app
    private(set) var lastCloseAllowed: Bool? = nil   // last respondToClose decision (conformance)

    // The logical window size (CSS pixels) the app last set, the source of truth for getSize. It is
    // NOT re-read from AppKit's live frame per call: DPI scaling and OS frame adjustments can perturb
    // the actual frame, so reading it would make setSize→getSize round-trip inexactly (contract §4.2
    // testability). setSize writes it; a real user resize adopts the new frame into it.
    private var logicalSize = NSSize(width: 1024, height: 720)
    private var applyingProgrammaticSize = false   // suppress adopting our own setSize's resize notification

    init(emitter: EventEmitter, conformance: Bool = false) {
        self.emitter = emitter
        self.conformance = conformance
    }

    func attach(_ window: NSWindow) {
        self.window = window
        window.delegate = self
        if let s = window.contentView?.bounds.size { logicalSize = s }   // seed from the real initial size
    }

    private func sizePayload() -> JSONValue {
        .object(["width": .number(Double(logicalSize.width)), "height": .number(Double(logicalSize.height))])
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
        // A real (user/OS) resize is authoritative — adopt it into the logical model. A resize
        // caused by our own setSize is not: the model already holds the requested logical size, and
        // adopting the possibly-adjusted frame would defeat the exact setSize→getSize round-trip.
        if !applyingProgrammaticSize, let s = window?.contentView?.bounds.size { logicalSize = s }
        emitter.emit("window.resize", sizePayload())
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

    func size() -> JSONValue { sizePayload() }

    func setSize(width: Double, height: Double) {
        // The logical size the app sets is authoritative for getSize, independent of what AppKit
        // does to the actual frame (§4.2). Store it, then request the frame; the resize notification
        // our own setContentSize triggers must not overwrite the model (see windowDidResize).
        logicalSize = NSSize(width: width, height: height)
        applyingProgrammaticSize = true
        window?.setContentSize(logicalSize)
        applyingProgrammaticSize = false
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
        lastCloseAllowed = allow
        // In conformance we record the decision but keep the off-screen window, so the suite can
        // assert allow-handling on both hosts without the suite host closing itself.
        if allow && !conformance { window?.close() }
    }
}
