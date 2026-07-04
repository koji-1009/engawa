import Foundation
import WebKit

// Serves the `app://` scheme (contract §5, §5a). Two authorities:
//   app://io/<token>   — the binary I/O channel (§5a): PUT writes, GET reads.
//   app://<root>/path  — the app asset directory, served with MIME by extension.
//
// The §5a PUT-body path is the vertical-slice spike (design.md "Known risks"):
// whether WKURLSchemeHandler delivers a fetch() request body at all is the open
// question this handler answers empirically.
final class AppSchemeHandler: NSObject, WKURLSchemeHandler {
    // Resolves the live asset root per request — it is the live A/B slot (§8), not a fixed dir.
    private let rootProvider: @Sendable () -> URL?
    private let ioTokens: IoTokenStore

    init(rootProvider: @escaping @Sendable () -> URL?, ioTokens: IoTokenStore) {
        self.rootProvider = rootProvider
        self.ioTokens = ioTokens
    }

    func webView(_ webView: WKWebView, start task: WKURLSchemeTask) {
        guard let url = task.request.url else {
            task.didFailWithError(NSError(domain: "engawa", code: 1))
            return
        }
        if url.host == "io" {
            handleIO(task)
        } else {
            handleAsset(task, url: url)
        }
    }

    func webView(_ webView: WKWebView, stop task: WKURLSchemeTask) {
        // Nothing long-running yet; slice responses complete synchronously.
    }

    // MARK: app://io — the binary I/O channel (§5a)

    private func handleIO(_ task: WKURLSchemeTask) {
        let req = task.request
        let method = req.httpMethod ?? "GET"

        if method == "OPTIONS" {   // CORS preflight
            respondData(task, Data(), mime: "text/plain", status: 204, cors: true)
            return
        }

        let token = req.url?.lastPathComponent ?? ""
        guard let entry = ioTokens.take(token) else {
            respondJSON(task, ["ok": false, "err": ["code": "EINVAL", "message": "invalid or expired io token"]], status: 200, cors: true)
            return
        }

        switch (method, entry.mode) {
        case ("PUT", .write):
            let bytes = requestBody(req)
            do {
                try bytes.write(to: URL(fileURLWithPath: entry.path))
                respondJSON(task, ["ok": true, "value": ["bytesWritten": bytes.count]], status: 200, cors: true)
            } catch {
                respondJSON(task, ["ok": false, "err": ["code": "EIO", "message": error.localizedDescription]], status: 200, cors: true)
            }
        case ("GET", .read):
            guard let data = try? Data(contentsOf: URL(fileURLWithPath: entry.path)) else {
                respondJSON(task, ["ok": false, "err": ["code": "EIO", "message": "read failed"]], status: 200, cors: true)
                return
            }
            respondData(task, data, mime: "application/octet-stream", status: 200, cors: true)
        default:
            respondJSON(task, ["ok": false, "err": ["code": "EINVAL", "message": "method/mode mismatch"]], status: 200, cors: true)
        }
    }

    private func requestBody(_ req: URLRequest) -> Data {
        if let body = req.httpBody { return body }
        guard let stream = req.httpBodyStream else { return Data() }
        stream.open(); defer { stream.close() }
        var bytes = Data()
        let bufSize = 1 << 16
        var buffer = [UInt8](repeating: 0, count: bufSize)
        while stream.hasBytesAvailable {
            let n = stream.read(&buffer, maxLength: bufSize)
            if n > 0 { bytes.append(buffer, count: n) } else { break }
        }
        return bytes
    }

    // MARK: app assets

    private func handleAsset(_ task: WKURLSchemeTask, url: URL) {
        guard let root = rootProvider() else {
            respondText(task, "no asset root configured", mime: "text/plain", status: 500)
            return
        }
        var rel = url.path
        if rel.isEmpty || rel == "/" { rel = "/index.html" }
        // Resolve within root; reject traversal outside it.
        let candidate = root.appendingPathComponent(rel).standardizedFileURL
        guard candidate.path.hasPrefix(root.standardizedFileURL.path) else {
            respondText(task, "forbidden", mime: "text/plain", status: 403)
            return
        }
        guard let data = try? Data(contentsOf: candidate) else {
            respondText(task, "not found: \(rel)", mime: "text/plain", status: 404)
            return
        }
        respondData(task, data, mime: mimeType(for: candidate.pathExtension), status: 200, csp: true)
    }

    // Default CSP (contract §7.3): confine document content to the app origin. Inline script
    // is dead by default — no 'unsafe-inline'. Host injection of __shell/shell.js uses the
    // native user-script path and is not subject to this policy.
    private let defaultCSP = "default-src app:; script-src app:"

    // MARK: response helpers

    private func respondJSON(_ task: WKURLSchemeTask, _ obj: [String: Any], status: Int, cors: Bool = false) {
        let data = (JSON.string(obj) ?? "{}").data(using: .utf8)!
        respondData(task, data, mime: "application/json", status: status, cors: cors)
    }

    private func respondText(_ task: WKURLSchemeTask, _ text: String, mime: String, status: Int) {
        respondData(task, text.data(using: .utf8)!, mime: mime, status: status)
    }

    private func respondData(_ task: WKURLSchemeTask, _ data: Data, mime: String, status: Int, cors: Bool = false, csp: Bool = false) {
        var headers = ["Content-Type": mime, "Content-Length": "\(data.count)"]
        if csp { headers["Content-Security-Policy"] = defaultCSP }
        if cors {
            headers["Access-Control-Allow-Origin"] = "*"
            headers["Access-Control-Allow-Methods"] = "GET, PUT, OPTIONS"
            headers["Access-Control-Allow-Headers"] = "*"
        }
        let response = HTTPURLResponse(url: task.request.url!, statusCode: status,
                                       httpVersion: "HTTP/1.1", headerFields: headers)!
        task.didReceive(response)
        task.didReceive(data)
        task.didFinish()
    }

    private func mimeType(for ext: String) -> String {
        switch ext.lowercased() {
        case "html", "htm": return "text/html; charset=utf-8"
        case "js", "mjs": return "text/javascript; charset=utf-8"
        case "css": return "text/css; charset=utf-8"
        case "json": return "application/json"
        case "png": return "image/png"
        case "jpg", "jpeg": return "image/jpeg"
        case "svg": return "image/svg+xml"
        case "wasm": return "application/wasm"
        default: return "application/octet-stream"
        }
    }
}
