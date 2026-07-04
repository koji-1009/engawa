import Cocoa
import EngawaKit

// Owns the NSWindow and the close protocol (contract §4.2). A user close attempt does
// not close the window; it emits window.closeRequested with a token and waits — the app
// answers via window.respondToClose(token, allow). No timeout (§4.2).
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

    init(emitter: EventEmitter) {
        self.emitter = emitter
    }

    func attach(_ window: NSWindow) {
        self.window = window
        window.delegate = self
    }

    // MARK: NSWindowDelegate

    func windowShouldClose(_ sender: NSWindow) -> Bool {
        beginClose()
        return false   // defer to the app; close only on respondToClose(allow: true)
    }

    func windowDidBecomeKey(_ notification: Notification) {
        emitter.emit("window.focus", .null)
    }

    func windowDidResignKey(_ notification: Notification) {
        emitter.emit("window.blur", .null)
    }

    // MARK: commands

    @discardableResult
    func beginClose() -> Int {
        tokenSeq += 1
        let token = tokenSeq
        pendingCloseTokens.insert(token)
        emitter.emit("window.closeRequested", .object(["token": .number(Double(token))]))
        return token
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
