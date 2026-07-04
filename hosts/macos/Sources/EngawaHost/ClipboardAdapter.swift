import Cocoa

// The `clipboard` namespace (spec/commands/clipboard.md). In conformance mode the host
// hands us a private, named pasteboard so the suite never disturbs the user's clipboard;
// in app mode it is the general pasteboard.
struct ClipboardAdapter: Adapter {
    let namespace = "clipboard"
    let pasteboard: NSPasteboard

    func handle(_ cmd: String, _ args: JSONValue) async throws -> JSONValue {
        switch cmd {
        case "writeText":
            guard let text = args.objectValue?["text"]?.stringValue else {
                throw AdapterError("EINVAL", "text required")
            }
            pasteboard.clearContents()
            pasteboard.setString(text, forType: .string)
            return .null
        case "readText":
            return .string(pasteboard.string(forType: .string) ?? "")
        default:
            throw AdapterError("ENOSYS", "unknown command: clipboard.\(cmd)")
        }
    }
}
