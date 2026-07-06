#pragma once
// Boot configuration, resolved from environment. The conformance driver (conformance/hosts/windows/
// driver.js) and the make-notes gate both drive the host purely through these variables — the same
// contract the macOS driver uses, so one suite runs on both.
#include <optional>
#include <string>

struct HostOptions {
    // Modes.
    bool conformance = false;  // ENGAWA_CONFORMANCE=1 — stdio control channel, test hooks live
    bool autotest = false;     // ENGAWA_AUTOTEST=1 — examples/notes self-drives the make-notes sequence
    std::optional<std::string> autotestUpdate;  // ENGAWA_AUTOTEST_UPDATE — app-update descriptor JSON

    // Roots.
    std::string appRoot;      // ENGAWA_APP_ROOT — initial asset tree (seeds slot A when update is active)
    std::string dataRoot;     // ENGAWA_DATA_ROOT — per-app data root (path.appData etc., engawa/ slots)
    std::string bundleRoot;   // ENGAWA_BUNDLE_ROOT — app bundle (engawa.json, sidecar allowlist §7.2)
    std::string shellJsPath;  // ENGAWA_SHELL_JS — shell.js to inject (§1); else resolved beside the exe

    // Trust + test hooks.
    std::string trustRootB64;                  // ENGAWA_TRUST_ROOT — embedded ed25519 public key (§7.1)
    std::optional<std::string> fakeEngineVersion;  // ENGAWA_FAKE_ENGINE_VERSION — substitute engine ver (§9)
    bool wipeStorage = false;                  // ENGAWA_WIPE_STORAGE=1 — clear WebView storage at boot (§10)
    std::optional<std::string> startUrl;       // ENGAWA_START_URL — override start URL (§6 non-app matrix)

    static HostOptions fromEnvironment();
};
