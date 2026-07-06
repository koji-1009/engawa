#pragma once
// Linux edition: the wire, the filesystem, GTK, GLib and WebKitGTK are ALL UTF-8 char strings
// (contract §2, spec/commands/fs.md), so — unlike the Windows host — nothing round-trips through
// UTF-16 and there is no ToWide/ToUtf8 dance. This header carries only the environment reader.
#include <cstdlib>
#include <optional>
#include <string>

// Read an environment variable. Empty optional means the variable is absent — distinct from
// present-but-empty (matches the Windows host's EnvOpt contract).
inline std::optional<std::string> EnvOpt(const char* key) {
    const char* v = std::getenv(key);
    if (!v) return std::nullopt;
    return std::string(v);
}
