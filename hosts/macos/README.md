# hosts/macos — Swift reference host

The reference host: Swift + WKWebView, on macOS's most-paved path. It implements the two protocol primitives (receive a string, evaluate a string), the boot handshake (contract §1), the `app://` scheme (§5, §5a), the adapter registration API (§3), and the built-in namespaces as in-tree adapters (§4). Boots in bootstrap stage 2.
