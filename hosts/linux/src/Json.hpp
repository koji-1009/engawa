#pragma once
// Wire-protocol JSON (contract §2) on nlohmann/json, plus the arg readers and the error type.
// Arg objects are untyped JSON; a missing/mistyped field is EINVAL per the command spec, so the
// readers centralize that mapping instead of scattering null checks through every handler
// (one place for the untyped-arg -> registry-code mapping).
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

using Json = nlohmann::json;

// The registry code an error maps to (spec/errors.md). A raw platform error MUST NOT cross the wire;
// every handler throws EngawaError with one of these instead. Dispatcher maps anything else to EIO.
struct EngawaError : std::exception {
    std::string code;
    std::string message;
    EngawaError(std::string c, std::string m) : code(std::move(c)), message(std::move(m)) {}
    const char* what() const noexcept override { return message.c_str(); }

    static EngawaError invalid(const std::string& m) { return {"EINVAL", m}; }
    static EngawaError noent(const std::string& m) { return {"ENOENT", m}; }
    static EngawaError nosys(const std::string& m) { return {"ENOSYS", m}; }
    static EngawaError io(const std::string& m) { return {"EIO", m}; }
};

// Arg readers over an untyped Json value (which may be null / not an object).
namespace ja {

inline const Json* field(const Json& a, const std::string& key) {
    if (!a.is_object()) return nullptr;
    auto it = a.find(key);
    if (it == a.end() || it->is_null()) return nullptr;
    return &*it;
}

inline const Json& obj(const Json& a) {
    if (!a.is_object()) throw EngawaError::invalid("object argument required");
    return a;
}

inline std::optional<std::string> optString(const Json& a, const std::string& key) {
    const Json* v = field(a, key);
    if (v && v->is_string()) return v->get<std::string>();
    return std::nullopt;
}

inline std::string reqString(const Json& a, const std::string& key) {
    auto s = optString(a, key);
    if (!s || s->empty()) throw EngawaError::invalid(key + " required");
    return *s;
}

// A string that may legitimately be "" (e.g. clipboard/fs contents): distinguishes "absent /
// not-a-string" (EINVAL) from "present but empty" (allowed).
inline std::string reqStringAllowEmpty(const Json& a, const std::string& key) {
    const Json* v = field(a, key);
    if (!v || !v->is_string()) throw EngawaError::invalid(key + " required");
    return v->get<std::string>();
}

inline bool optBool(const Json& a, const std::string& key, bool fallback = false) {
    const Json* v = field(a, key);
    if (v && v->is_boolean()) return v->get<bool>();
    return fallback;
}

inline bool tryGetDouble(const Json& a, const std::string& key, double& out) {
    const Json* v = field(a, key);
    if (!v || !v->is_number()) return false;
    out = v->get<double>();
    return true;
}

inline int reqInt(const Json& a, const std::string& key) {
    double d;
    if (!tryGetDouble(a, key, d)) throw EngawaError::invalid(key + " required");
    return static_cast<int>(d);
}

}  // namespace ja
