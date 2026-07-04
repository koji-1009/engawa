import EngawaHostCore
import EngawaKit
import EngawaSQLite

// The reference / conformance host: the core plus the `sqlite` reference adapter (the conformance
// suite exercises sqlite, and this is the example composition). Per-app hosts the CLI generates
// pass their own declared adapters here instead.
EngawaRuntime.run(appAdapters: [SqliteAdapter()])
