// Default app-adapter composition for the in-tree reference host (make host-windows / build.ps1),
// which the conformance suite and the make-notes gate build: it registers the reference `sqlite`
// adapter so every namespace the suite exercises is present. `engawa dev/build` does NOT compile this
// file — it generates a Compose that registers exactly the adapters the app declared.
#include "Compose.hpp"

#include "adapters/Adapters.hpp"

void registerAppAdapters(Dispatcher& dispatcher, IEventEmitter* emitter) {
    dispatcher.registerAdapter(makeSqliteAdapter(), emitter);
}
