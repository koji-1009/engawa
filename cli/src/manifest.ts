import { readFileSync } from "node:fs";
import { join } from "node:path";
import { CliError } from "./log.ts";

// The app manifest (engawa.json). Identity + version, the §7.2 sidecar allowlist, and §7.3
// CSP relaxations.
export interface Manifest {
  id: string;
  version: string;
  name?: string;
  sidecars: string[];
  csp?: Record<string, string[]>;
}

export function readManifest(dir: string): Manifest {
  let raw: string;
  try {
    raw = readFileSync(join(dir, "engawa.json"), "utf8");
  } catch {
    throw new CliError(`no engawa.json in ${dir} — run \`engawa new\` to scaffold an app`);
  }

  let parsed: unknown;
  try {
    parsed = JSON.parse(raw);
  } catch (e) {
    throw new CliError(`engawa.json is not valid JSON: ${(e as Error).message}`);
  }

  if (typeof parsed !== "object" || parsed === null) throw new CliError("engawa.json must be a JSON object");
  const obj = parsed as Record<string, unknown>;

  const id = obj["id"];
  const version = obj["version"];
  if (typeof id !== "string" || id === "") throw new CliError("engawa.json: `id` (non-empty string) is required");
  if (typeof version !== "string" || version === "") throw new CliError("engawa.json: `version` (non-empty string) is required");

  const manifest: Manifest = {
    id,
    version,
    sidecars: Array.isArray(obj["sidecars"]) ? obj["sidecars"].filter((s): s is string => typeof s === "string") : [],
  };
  if (typeof obj["name"] === "string") manifest.name = obj["name"];
  if (typeof obj["csp"] === "object" && obj["csp"] !== null) manifest.csp = obj["csp"] as Record<string, string[]>;
  return manifest;
}

export function appName(manifest: Manifest): string {
  return manifest.name ?? manifest.id.split(".").pop() ?? "App";
}
