#pragma once
// The host obligations the `update` adapter delegates to (contract §7.1 + §8). These are host
// duties, NOT the adapter's: a host is non-conformant without signature verification before anything
// lands under app://, and without the atomic A/B slot swap (CLAUDE.md, adapters/update/spec.md).
//
//   <appData>/engawa/
//     slots/{a,b}/   asset trees; exactly one is live
//     current        one-line pointer to the live slot
//     pending        slot awaiting adoption (single atomic write — the only commit point)
//     health         { bootingSlot, attempts }
//     version        the live slot's app version
#include <optional>
#include <string>

#include "Json.hpp"

class UpdateHost {
public:
    UpdateHost(const std::string& dataRoot, const std::string& appSeedRoot,
               const std::string& initialVersion, const std::string& trustRootB64);

    // The slot currently serving app:// (contract §8 — the app:// root is an indirection).
    std::string liveRoot() const;

    Json bootDecision();  // re-run the boot-time slot decision (§8)
    Json status();
    Json stageAppUpdate(const std::string& payloadPath, const std::string& hash,
                        const std::string& signature, const std::string& version);
    void confirmBoot();

private:
    struct Pending {
        std::string slot;
        std::string version;
    };

    void initialize(const std::string& appSeedRoot, const std::string& initialVersion);
    bool verifySignature(const std::string& digest, const std::string& signatureB64);

    std::string slotDir(const std::string& s) const;
    std::string currentFile() const;
    std::string pendingFile() const;
    std::string healthFile() const;
    std::string versionFile() const;
    static std::string other(const std::string& s) { return s == "a" ? "b" : "a"; }

    std::optional<Pending> readPending() const;
    std::optional<Json> readHealth() const;

    std::string root_;
    std::string slots_;
    std::string trustRootB64_;
    std::string current_ = "a";
    std::string bootingSlot_ = "a";
};
