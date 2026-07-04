import { existsSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { CliError } from "./log.ts";

// The Engawa reference implementation: the repo (or an SDK dir) that holds the macOS host
// package, the adapter SDK (hosts/macos-kit), the adapters, and shell.js. Set ENGAWA_HOME, or
// let the CLI find it by walking up from its own location.
export function findEngawaHome(): string {
  const fromEnv = process.env["ENGAWA_HOME"];
  if (fromEnv !== undefined && fromEnv !== "") {
    const dir = resolve(fromEnv);
    if (!isEngawaHome(dir)) throw new CliError(`ENGAWA_HOME=${dir} is not an Engawa home (missing hosts/macos-kit or shell-js/shell.js)`);
    return dir;
  }
  let dir = dirname(fileURLToPath(import.meta.url));
  for (let i = 0; i < 10; i++) {
    if (isEngawaHome(dir)) return dir;
    const parent = dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  throw new CliError("could not locate the Engawa home; set ENGAWA_HOME to the repo root");
}

function isEngawaHome(dir: string): boolean {
  return existsSync(join(dir, "hosts", "macos-kit")) && existsSync(join(dir, "shell-js", "shell.js"));
}
