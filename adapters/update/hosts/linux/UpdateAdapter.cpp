// The `update` namespace on Windows (adapters/update/spec.md). Contract-coupled: this adapter owns
// delivery + policy, but trust (§7.1) and the atomic A/B slot swap (§8) are HOST obligations it
// delegates to UpdateHost. One manifest, two modes: app-update (signed asset swap) and full-update
// (verified installer + handoff event; the OS executes the replacement, out of scope).
#include <unordered_set>

#include "EngineFloor.hpp"
#include "adapters/Adapters.hpp"

namespace {

std::unordered_set<std::string> toSet(const Json& n) {
    std::unordered_set<std::string> s;
    if (n.is_array())
        for (auto& x : n)
            if (x.is_string()) s.insert(x.get<std::string>());
    return s;
}

class UpdateAdapter : public IAdapter {
public:
    UpdateAdapter(UpdateHost& host, const HostOptions& opts) : host_(host), conformance_(opts.conformance) {}

    std::string ns() const override { return "update"; }
    void attach(IEventEmitter* emitter) override { emitter_ = emitter; }

    Json handle(const std::string& command, const Json& args) override {
        if (command == "status") return host_.status();
        if (command == "evaluate") return evaluate(args);
        if (command == "stageAppUpdate") return stageAppUpdate(args);
        if (command == "stageBaseUpdate") return stageBaseUpdate(args);
        if (command == "confirmBoot") { host_.confirmBoot(); return Json(nullptr); }
        if (command == "install") return Json{{"handoff", true}};
        if (command == "__relaunch" && conformance_) return host_.bootDecision();
        throw EngawaError::nosys("update." + command);
    }

private:
    // §8 compatibility rule: app-update if the running base satisfies the app's contract + every
    // required capability; else full-update (and announce a verified installer is available).
    Json evaluate(const Json& args) {
        const Json& o = ja::obj(args);
        if (!o.contains("manifest") || !o["manifest"].is_object() ||
            !o["manifest"].contains("app") || !o["manifest"]["app"].is_object())
            throw EngawaError::invalid("manifest.app required");
        if (!o.contains("provided") || !o["provided"].is_object())
            throw EngawaError::invalid("provided required");
        const Json& app = o["manifest"]["app"];
        const Json& provided = o["provided"];

        std::string version = app.value("version", "0.0.0");
        std::string contractRequired = app.value("contractRequired", "0.0.0");
        std::string contractProvided = provided.value("contractProvided", "0.0.0");

        auto required = toSet(app.contains("capabilitiesRequired") ? app["capabilitiesRequired"] : Json());
        auto have = toSet(provided.contains("capabilities") ? provided["capabilities"] : Json());

        bool contractOk = EngineFloor::compare(contractProvided, contractRequired) >= 0;
        bool capsOk = true;
        for (auto& r : required)
            if (!have.count(r)) { capsOk = false; break; }

        std::string mode;
        if (contractOk && capsOk) {
            mode = "app-update";
        } else {
            mode = "full-update";  // classification only (§153) — readyToInstall comes from stageBaseUpdate
        }
        return Json{{"mode", mode}, {"version", version}};
    }

    Json stageAppUpdate(const Json& args) {
        return host_.stageAppUpdate(ja::reqString(args, "payloadPath"), ja::reqString(args, "hash"),
                                    ja::reqString(args, "signature"), ja::reqString(args, "version"));
    }

    // §153: verify the base installer BEFORE announcing it; readyToInstall fires only on success
    // (a bad signature throws ESIGNATURE and emits nothing).
    Json stageBaseUpdate(const Json& args) {
        Json r = host_.verifyBaseInstaller(ja::reqString(args, "payloadPath"), ja::reqString(args, "hash"),
                                           ja::reqString(args, "signature"));
        std::string version = ja::obj(args).value("version", std::string("0.0.0"));
        if (emitter_) emitter_->emit("update.readyToInstall", Json{{"version", version}});
        return r;
    }

    UpdateHost& host_;
    bool conformance_;
    IEventEmitter* emitter_ = nullptr;
};

}  // namespace

std::unique_ptr<IAdapter> makeUpdateAdapter(UpdateHost& host, const HostOptions& opts) {
    return std::make_unique<UpdateAdapter>(host, opts);
}
