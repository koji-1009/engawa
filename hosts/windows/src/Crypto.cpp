#include "Crypto.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <vector>

extern "C" {
#include "tweetnacl.h"
// tweetnacl.c references randombytes (used only by keypair/box, never by verify). Provide it so the
// TU links; verification never calls it.
void randombytes(unsigned char*, unsigned long long) {}
}

namespace Crypto {

std::string sha256(const std::string& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
    DWORD objLen = 0, cb = 0;
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &cb, 0);
    std::vector<UCHAR> obj(objLen);
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string out;  // stays empty on any failure so it can never masquerade as a real digest
    if (BCryptCreateHash(alg, &hash, obj.data(), objLen, nullptr, 0, 0) == 0) {
        std::string digest(32, '\0');
        if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                           static_cast<ULONG>(data.size()), 0) == 0 &&
            BCryptFinishHash(hash, reinterpret_cast<PUCHAR>(digest.data()), 32, 0) == 0)
            out = std::move(digest);
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return out;
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
    if (pubkey.size() != 32 || sig.size() != 64) return false;
    // tweetnacl verifies an ATTACHED signature (sig || message); reconstruct it and check the opened
    // message length equals the input message length.
    std::vector<unsigned char> sm(sig.size() + message.size());
    memcpy(sm.data(), sig.data(), sig.size());
    if (!message.empty()) memcpy(sm.data() + sig.size(), message.data(), message.size());
    std::vector<unsigned char> m(sm.size());
    unsigned long long mlen = 0;
    int rc = crypto_sign_open(m.data(), &mlen, sm.data(), sm.size(),
                              reinterpret_cast<const unsigned char*>(pubkey.data()));
    return rc == 0 && mlen == message.size();
}

}  // namespace Crypto
