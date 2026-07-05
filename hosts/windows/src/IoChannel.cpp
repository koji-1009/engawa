#include "IoChannel.hpp"

#include <windows.h>
#include <objbase.h>

#include <cstdio>

namespace {
// A URL-safe opaque id from a fresh GUID (host process, not a resumable workflow — randomness is fine).
std::string newId() {
    GUID g{};
    CoCreateGuid(&g);
    char buf[33];
    std::snprintf(buf, sizeof(buf), "%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
                  static_cast<unsigned long>(g.Data1), g.Data2, g.Data3,
                  g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
                  g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return std::string(buf);
}
}  // namespace

std::string IoChannel::mint(const std::string& path, bool write) {
    std::lock_guard<std::mutex> lk(mu_);
    sweep();
    std::string id = newId();
    tokens_[id] = IoToken{id, path, write, GetTickCount64()};
    return id;
}

std::optional<IoToken> IoChannel::consume(const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = tokens_.find(id);
    if (it == tokens_.end()) return std::nullopt;
    IoToken t = it->second;
    tokens_.erase(it);
    if (GetTickCount64() - t.createdMs > IdleMs) return std::nullopt;
    return t;
}

void IoChannel::sweep() {
    unsigned long long now = GetTickCount64();
    for (auto it = tokens_.begin(); it != tokens_.end();) {
        if (now - it->second.createdMs > IdleMs) it = tokens_.erase(it);
        else ++it;
    }
}
