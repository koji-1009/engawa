import { mkdirSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { parseArgs } from "../args.ts";
import { generateKeypair } from "../crypto.ts";
import { log } from "../log.ts";

export function cmdKeygen(argv: string[]): void {
  const { flags } = parseArgs(argv);
  const outDir = resolve(flags["out"] ?? ".");
  mkdirSync(outDir, { recursive: true });

  const kp = generateKeypair();
  writeFileSync(join(outDir, "trust-root.txt"), kp.trustRoot);
  writeFileSync(join(outDir, "engawa-signing.key"), kp.privatePem, { mode: 0o600 });

  log.ok(`wrote trust-root.txt (public, embedded in builds) and engawa-signing.key (private)`);
  log.warn("keep engawa-signing.key secret — it signs your app updates (§7.1)");
}
