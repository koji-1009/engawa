// Default app-adapter composition for the in-tree reference host (make host-linux / build.sh),
// which the conformance suite and the make-notes gate build: it registers the reference `sqlite`
// adapter so every namespace the suite exercises is present. `engawa dev/build` does NOT compile this
// file — it generates a Compose that registers exactly the adapters the app declared.
#include "Compose.hpp"

#include "adapters/Adapters.hpp"

void registerAppAdapters(Dispatcher& dispatcher, IEventEmitter* emitter) {
    dispatcher.registerAdapter(makeSqliteAdapter(), emitter);
}

// Reference/dev host: no compiled-in trust root. The env-provided root (conformance driver,
// make-notes gate, `engawa dev`) is used instead (§7.1 permits an env trust root for dev/conformance).
std::string bakedTrustRoot() { return {}; }
