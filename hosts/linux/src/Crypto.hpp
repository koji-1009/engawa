#pragma once
// The cryptography the update trust root needs (contract §7.1): SHA-256 over the payload (libsodium
// CNG) and ed25519 signature verification (libsodium). Node signs the sha256 digest directly
// (PureEdDSA over the 32-byte digest), so verification is over the digest, not the file.
#include <string>

namespace Crypto {

// Raw 32-byte SHA-256 digest of `data`.
std::string sha256(const std::string& data);

// Lowercase hex of raw bytes.
std::string toHex(const std::string& bytes);

// Standard base64 decode; empty string on malformed input.
std::string base64Decode(const std::string& b64);

// Verify a detached ed25519 signature (`sig`, 64 bytes) of `message` against `pubkey` (32 bytes).
bool ed25519Verify(const std::string& pubkey, const std::string& message, const std::string& sig);

}  // namespace Crypto
