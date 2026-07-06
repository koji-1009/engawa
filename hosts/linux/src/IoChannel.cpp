#include "IoChannel.hpp"

#include <sodium.h>

#include <chrono>

namespace {

// Monotonic milliseconds — token expiry is a duration, so a steady clock (never walks backward on an
// NTP step) is the right source.
unsigned long long nowMs() {
    return static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

// A URL-safe opaque id: 16 CSPRNG bytes as hex. Uses libsodium (already linked for §7.1) so the
// token is unguessable regardless of std::random_device's quality — a predictable id would let a
// hostile in-page script race a legitimate fetch for the target file (§5a).
std::string newId() {
    static const bool inited = (sodium_init() >= 0);
    (void)inited;
    unsigned char raw[16];
    randombytes_buf(raw, sizeof(raw));
    static const char* hex = "0123456789abcdef";
    std::string id;
    id.reserve(32);
    for (unsigned char b : raw) {
        id.push_back(hex[b >> 4]);
        id.push_back(hex[b & 0xf]);
    }
    return id;
}

}  // namespace

std::string IoChannel::mint(const std::string& path, bool write) {
    std::lock_guard<std::mutex> lk(mu_);
    sweep();
    std::string id = newId();
    tokens_[id] = IoToken{id, path, write, nowMs()};
    return id;
}

std::optional<IoToken> IoChannel::consume(const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = tokens_.find(id);
    if (it == tokens_.end()) return std::nullopt;
    IoToken t = it->second;
    tokens_.erase(it);
    if (nowMs() - t.createdMs > IdleMs) return std::nullopt;
    return t;
}

void IoChannel::sweep() {
    unsigned long long now = nowMs();
    for (auto it = tokens_.begin(); it != tokens_.end();) {
        if (now - it->second.createdMs > IdleMs) it = tokens_.erase(it);
        else ++it;
    }
}
