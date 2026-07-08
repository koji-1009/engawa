import Foundation
import Cocoa
import EngawaKit

// The `tray` namespace (spec/commands/tray.md) — a per-app status-area icon. Like dialog/notification,
// under conformance it records intent and drives events via the __click/__menuClick hooks (no real
// UI, which is not headless-testable); in app mode it puts a real NSStatusItem in the menu bar and
// reports activations as events.
final class TrayAdapter: Adapter, @unchecked Sendable {
    let namespace = "tray"
    private let conformance: Bool
    private var emitter: EventEmitter?
    private let controller = TrayController()

    init(conformance: Bool) { self.conformance = conformance }

    func attach(_ emitter: EventEmitter) {
        self.emitter = emitter
        Task { @MainActor in self.controller.emitter = emitter }
    }

    func handle(_ cmd: String, _ args: JSONValue) async throws -> JSONValue {
        let obj = args.objectValue ?? [:]
        switch cmd {
        case "set":
            let tooltip = obj["tooltip"]?.stringValue
            let menu = try Self.parseMenu(obj["menu"])
            if !conformance { await controller.set(tooltip: tooltip, menu: menu) }
            return .null

        case "remove":
            if !conformance { await controller.remove() }
            return .null

        case "__click" where conformance:
            emitter?.emit("tray.clicked", .null)
            return .null

        case "__menuClick" where conformance:
            guard let id = obj["id"]?.stringValue else { throw AdapterError("EINVAL", "id required") }
            emitter?.emit("tray.menuClicked", .object(["id": .string(id)]))
            return .null

        default:
            throw AdapterError("ENOSYS", "unknown command: tray.\(cmd)")
        }
    }

    // `menu` is an array of `{ id, label }`; an item with no `label` is a separator.
    static func parseMenu(_ v: JSONValue?) throws -> [(id: String, label: String?)] {
        guard let v = v else { return [] }
        guard let arr = v.arrayValue else { throw AdapterError("EINVAL", "menu must be an array") }
        return try arr.map { item in
            guard let o = item.objectValue else { throw AdapterError("EINVAL", "menu item must be an object") }
            let label = o["label"]?.stringValue
            let id = o["id"]?.stringValue ?? ""
            if label != nil && id.isEmpty { throw AdapterError("EINVAL", "menu item needs an id") }
            return (id: id, label: label)
        }
    }
}

// AppKit status item, main-actor isolated (NSStatusItem is UI). Only used in app mode.
@MainActor
final class TrayController: NSObject {
    var emitter: EventEmitter?
    private var statusItem: NSStatusItem?
    private var itemIds: [Int: String] = [:]

    // Constructed from the (nonisolated) adapter; the UI methods below are the main-actor work.
    nonisolated override init() { super.init() }

    func set(tooltip: String?, menu: [(id: String, label: String?)]) {
        let item = statusItem ?? NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        statusItem = item
        item.button?.title = "●"   // default template glyph; app-level theming is a later refinement
        item.button?.toolTip = tooltip
        item.button?.target = self
        item.button?.action = #selector(clicked)

        itemIds.removeAll()
        if menu.isEmpty {
            item.menu = nil   // no menu → a plain click fires tray.clicked
        } else {
            let m = NSMenu()
            for (i, entry) in menu.enumerated() {
                if let label = entry.label {
                    let mi = NSMenuItem(title: label, action: #selector(menuClicked(_:)), keyEquivalent: "")
                    mi.target = self
                    mi.tag = i
                    itemIds[i] = entry.id
                    m.addItem(mi)
                } else {
                    m.addItem(.separator())
                }
            }
            item.menu = m   // a menu opens on click; selections fire tray.menuClicked
        }
    }

    func remove() {
        if let item = statusItem { NSStatusBar.system.removeStatusItem(item) }
        statusItem = nil
        itemIds.removeAll()
    }

    @objc private func clicked() { emitter?.emit("tray.clicked", .null) }

    @objc private func menuClicked(_ sender: NSMenuItem) {
        if let id = itemIds[sender.tag] { emitter?.emit("tray.menuClicked", .object(["id": .string(id)])) }
    }
}
