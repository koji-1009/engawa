import { readFileSync } from "node:fs";
import { join } from "node:path";
import { CliError } from "./log.ts";

// A statically-composed adapter (docs/design.md "static composition"). Referenced by local
// path or by git commit hash; the CLI compiles the declared adapters into the app's host.
export interface AdapterRef {
  package: string; // SwiftPM package identity (e.g. "sqlite")
  product: string; // library product to import (e.g. "EngawaSQLite")
  register: string; // Swift expression constructing the adapter (e.g. "SqliteAdapter()")
  path?: string; // local path to the adapter package, relative to the app dir
  url?: string; // ...or a git URL
  revision?: string; // ...with this commit (required with url — pin to a hash, design.md)
}

// The app manifest (engawa.json). Identity + version, the adapters compiled into the host,
// the §7.2 sidecar allowlist, and §7.3 CSP relaxations.
export interface Manifest {
  id: string;
  version: string;
  name?: string;
  adapters: AdapterRef[];
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
    adapters: parseAdapters(obj["adapters"]),
    sidecars: Array.isArray(obj["sidecars"]) ? obj["sidecars"].filter((s): s is string => typeof s === "string") : [],
  };
  if (typeof obj["name"] === "string") manifest.name = obj["name"];
  if (typeof obj["csp"] === "object" && obj["csp"] !== null) manifest.csp = obj["csp"] as Record<string, string[]>;
  return manifest;
}

// The register/product fields are interpolated into generated Swift, so constrain them: product
// is a module identifier; register is a single constructor expression, no statement breakers.
const IDENTIFIER = /^[A-Za-z_][A-Za-z0-9_]*$/;
const CONSTRUCTOR = /^[A-Za-z_][A-Za-z0-9_.]*\([^)('"`;{}[\]]*\)$/;

// SwiftPM derives a package's identity from its location's last path component (lowercased),
// minus a trailing .git for git URLs. The manifest's `package` must equal that, or the generated
// `.product(package:)` won't resolve.
function identityOf(ref: { path?: string; url?: string }): string {
  // URLs always use "/"; local paths may use the platform separator ("\" on Windows), so split on
  // both. Strip trailing separators first so a path ending in a slash still yields the basename.
  const loc = (ref.url ?? ref.path ?? "").replace(/[/\\]+$/, "");
  return (loc.split(/[/\\]/).pop() ?? "").replace(/\.git$/i, "").toLowerCase();
}

function parseAdapters(raw: unknown): AdapterRef[] {
  if (raw === undefined) return [];
  if (!Array.isArray(raw)) throw new CliError("engawa.json: `adapters` must be an array");
  return raw.map((entry, i): AdapterRef => {
    if (typeof entry !== "object" || entry === null) throw new CliError(`engawa.json: adapters[${i}] must be an object`);
    const a = entry as Record<string, unknown>;
    for (const key of ["package", "product", "register"]) {
      if (typeof a[key] !== "string" || a[key] === "") throw new CliError(`engawa.json: adapters[${i}].${key} (string) is required`);
    }
    if (!IDENTIFIER.test(a["product"] as string)) throw new CliError(`engawa.json: adapters[${i}].product must be a module identifier`);
    if (!CONSTRUCTOR.test(a["register"] as string)) throw new CliError(`engawa.json: adapters[${i}].register must be a single constructor expression, e.g. "SqliteAdapter()"`);

    const hasPath = typeof a["path"] === "string" && a["path"] !== "";
    const hasGit = typeof a["url"] === "string" && a["url"] !== "";
    if (hasPath === hasGit) throw new CliError(`engawa.json: adapters[${i}] needs exactly one of a non-empty \`path\` or \`url\``);
    if (hasGit && (typeof a["revision"] !== "string" || a["revision"] === "")) throw new CliError(`engawa.json: adapters[${i}].revision (a commit hash) is required with \`url\``);

    const ref: AdapterRef = { package: a["package"] as string, product: a["product"] as string, register: a["register"] as string };
    if (hasPath) ref.path = a["path"] as string;
    if (hasGit) { ref.url = a["url"] as string; ref.revision = a["revision"] as string; }

    const identity = identityOf(ref);
    if (ref.package.toLowerCase() !== identity) {
      throw new CliError(`engawa.json: adapters[${i}].package "${ref.package}" must match the ${hasGit ? "git repo" : "path"} basename "${identity}" (SwiftPM identity), or the product won't resolve`);
    }
    return ref;
  });
}

export function appName(manifest: Manifest): string {
  return manifest.name ?? manifest.id.split(".").pop() ?? "App";
}
