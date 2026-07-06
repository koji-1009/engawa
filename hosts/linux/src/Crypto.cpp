#include "Crypto.hpp"

#include <sodium.h>

namespace {
// libsodium must be initialized once before use; sodium_init() is idempotent and thread-safe to gate
// behind a function-local static.
bool ensureSodium() {
    static bool ok = (sodium_init() >= 0);
    return ok;
}
}  // namespace

namespace Crypto {

std::string sha256(const std::string& data) {
    if (!ensureSodium()) return {};
    std::string digest(crypto_hash_sha256_BYTES, '\0');  // 32 bytes
    crypto_hash_sha256(reinterpret_cast<unsigned char*>(digest.data()),
                       reinterpret_cast<const unsigned char*>(data.data()), data.size());
    return digest;
}

std::string toHex(const std::string& bytes) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) {
        out.push_back(hex[c >> 4]);
        out.push_back(hex[c & 0xf]);
    }
    return out;
}

std::string base64Decode(const std::string& b64) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    int buf = 0, bits = 0;
    for (char c : b64) {
        if (c == '=' || c == '\r' || c == '\n' || c == ' ') continue;
        int v = val(c);
        if (v < 0) return {};  // malformed
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buf >> bits) & 0xff));
        }
    }
    return out;
}

bool ed25519Verify(const std::string& pubkey, const std::string& message, const std::string& sig) {
    if (!ensureSodium()) return false;
    if (pubkey.size() != crypto_sign_PUBLICKEYBYTES || sig.size() != crypto_sign_BYTES) return false;  // 32 / 64
    // Node signs the sha256 digest directly (PureEdDSA over the 32-byte digest), so verification is a
    // detached ed25519 check over `message` (the digest), not the payload file.
    return crypto_sign_verify_detached(reinterpret_cast<const unsigned char*>(sig.data()),
                                       reinterpret_cast<const unsigned char*>(message.data()), message.size(),
                                       reinterpret_cast<const unsigned char*>(pubkey.data())) == 0;
}

}  // namespace Crypto
