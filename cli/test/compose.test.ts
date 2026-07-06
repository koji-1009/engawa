// The generated Windows compose TU (host.ts) carries two contract-critical facts: which adapters the
// app declared (§3 static composition) and the app's update trust root (§7.1). §7.1 requires the trust
// root to be COMPILED IN, not read from a swappable file beside the exe — so these assertions guard
// that `engawa build` bakes the key into bakedTrustRoot() rather than leaving it empty or on disk.
import assert from "node:assert/strict";
import { test } from "node:test";
import { composeCpp } from "../src/host.ts";

const KEY = "TESTdGVzdHRydXN0cm9vdGtleWJhc2U2NDA5ODc2NTQzMjE="; // a base64-shaped fake ed25519 pubkey

test("bakes the app's trust root into bakedTrustRoot()", () => {
  const cpp = composeCpp([], KEY);
  assert.match(cpp, /std::string bakedTrustRoot\(\) \{ return "TESTdGVzdHRydXN0cm9vdGtleWJhc2U2NDA5ODc2NTQzMjE="; \}/);
});

test("emits an empty trust root when the app publishes no updates (dev/reference host uses env)", () => {
  const cpp = composeCpp([]);
  assert.match(cpp, /std::string bakedTrustRoot\(\) \{ return ""; \}/);
  assert.doesNotMatch(cpp, /trust-root\.txt/); // never a runtime file read
});

test("registers exactly the declared adapters", () => {
  const cpp = composeCpp([{ factory: "makeSqliteAdapter", pkg: "sqlite", localDir: "/x" }], KEY);
  assert.match(cpp, /std::unique_ptr<IAdapter> makeSqliteAdapter\(\);/);
  assert.match(cpp, /dispatcher\.registerAdapter\(makeSqliteAdapter\(\), emitter\);/);
});

test("rejects a non-base64 trust root rather than emitting an unsafe C literal", () => {
  assert.throws(() => composeCpp([], 'evil"); system("rm -rf /"); //'), /not base64/);
});
