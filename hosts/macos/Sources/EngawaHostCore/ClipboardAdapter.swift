import Cocoa
import EngawaKit

// The `clipboard` namespace (spec/commands/clipboard.md). In conformance mode the host
// hands us a private, named pasteboard so the suite never disturbs the user's clipboard;
// in app mode it is the general pasteboard.
struct ClipboardAdapter: Adapter, @unchecked Sendable {   // NSPasteboard is used only on the main actor
    let namespace = "clipboard"
    let pasteboard: NSPasteboard

    func handle(_ cmd: String, _ args: JSONValue) async throws -> JSONValue {
        switch cmd {
        case "writeText":
            guard let text = args.objectValue?["text"]?.stringValue else {
                throw AdapterError("EINVAL", "text required")
            }
            await MainActor.run {
                pasteboard.clearContents()
                pasteboard.setString(text, forType: .string)
            }
            return .null
        case "readText":
            return .string(await MainActor.run { pasteboard.string(forType: .string) ?? "" })
        default:
            throw AdapterError("ENOSYS", "unknown command: clipboard.\(cmd)")
        }
    }
}
