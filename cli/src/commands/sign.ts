import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { parseArgs } from "../args.ts";
import { signPayload } from "../crypto.ts";
import { CliError } from "../log.ts";

// Sign an app-update payload (a tarball) → the {hash, signature} an update manifest carries.
// Machine-readable JSON on stdout.
export function cmdSign(argv: string[]): void {
  const { positionals, flags } = parseArgs(argv);
  const payload = positionals[0];
  if (payload === undefined) throw new CliError("usage: engawa sign <payload.tar> --key <engawa-signing.key> [--version <v>]");
  const key = flags["key"];
  if (key === undefined) throw new CliError("sign: --key <engawa-signing.key> is required");

  const sig = signPayload(resolve(payload), readFileSync(resolve(key), "utf8"));
  const out: Record<string, string> = { hash: sig.hash, signature: sig.signature };
  if (flags["version"] !== undefined) out["version"] = flags["version"];
  process.stdout.write(JSON.stringify(out) + "\n");
}
