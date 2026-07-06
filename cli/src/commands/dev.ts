import { existsSync, mkdirSync, readFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { parseArgs } from "../args.ts";
import { run } from "../exec.ts";
import { buildHost } from "../host.ts";
import { CliError, log } from "../log.ts";
import { readManifest } from "../manifest.ts";
import { findEngawaHome } from "../paths.ts";
import { buildApp } from "./build.ts";

// Build (debug) and launch the app in the foreground; Ctrl-C to quit.
export async function cmdDev(argv: string[]): Promise<void> {
  if (process.platform === "win32") return devWindows(argv);

  const bundle = await buildApp(argv, { dev: true });
  log.step(`launching ${bundle}`);
  await run(join(bundle, "Contents", "MacOS", "EngawaHost"), []);
}

// Windows has no .app bundle: build the native host, then launch EngawaHost.exe in place with the
// app's assets/shell.js/data wired through the environment (the same variables the make-notes gate
// uses). A normal (non-conformance) launch shows the window.
async function devWindows(argv: string[]): Promise<void> {
  const { flags } = parseArgs(argv);
  const appDir = resolve(flags["dir"] ?? ".");
  const manifest = readManifest(appDir);
  const assets = join(appDir, "app");
  if (!existsSync(join(assets, "index.html"))) {
    throw new CliError(`${assets}/index.html not found — an Engawa app serves its assets from app/`);
  }

  const home = findEngawaHome();
  const hostBinary = await buildHost(home, appDir, manifest, "debug");

  const dataRoot = join(appDir, "build", "data");
  mkdirSync(dataRoot, { recursive: true });

  const env: NodeJS.ProcessEnv = {
    ...process.env,
    ENGAWA_APP_ROOT: assets,
    ENGAWA_SHELL_JS: join(home, "shell-js", "shell.js"),
    ENGAWA_DATA_ROOT: dataRoot,
    ENGAWA_BUNDLE_ROOT: appDir, // engawa.json + declared sidecars' bin/ (contract §7.2)
  };
  const trustRoot = join(appDir, "trust-root.txt"); // contract §7.1, present iff the app publishes updates
  if (existsSync(trustRoot)) env["ENGAWA_TRUST_ROOT"] = readFileSync(trustRoot, "utf8").trim();

  log.step(`launching ${hostBinary}`);
  await run(hostBinary, [], { env });
}
