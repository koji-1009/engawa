import { copyFileSync, cpSync, existsSync, mkdirSync, rmSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { parseArgs } from "../args.ts";
import { toolExists, run } from "../exec.ts";
import { CliError, log } from "../log.ts";
import { appName, readManifest, type Manifest } from "../manifest.ts";
import { findEngawaHome } from "../paths.ts";

export interface BuildOptions {
  dev?: boolean; // debug config + skip release optimizations
}

// Build the app into a codesigned .app bundle and return its path.
export async function buildApp(argv: string[], options: BuildOptions = {}): Promise<string> {
  const { flags } = parseArgs(argv);
  const appDir = resolve(flags["dir"] ?? ".");
  const manifest = readManifest(appDir);
  const assets = join(appDir, "app");
  if (!existsSync(join(assets, "index.html"))) {
    throw new CliError(`${assets}/index.html not found — an Engawa app serves its assets from app/`);
  }

  if (!(await toolExists("swift"))) throw new CliError("swift not found — install Xcode / the Swift toolchain");
  const home = findEngawaHome();
  const config = options.dev === true ? "debug" : "release";

  log.step(`building the macOS host (${config})`);
  await run("swift", ["build", "--package-path", join(home, "hosts", "macos"), "-c", config]);
  const hostBinary = join(home, "hosts", "macos", ".build", config, "EngawaHost");
  if (!existsSync(hostBinary)) throw new CliError(`host binary not found at ${hostBinary}`);

  const name = appName(manifest);
  const outDir = resolve(flags["out"] ?? join(appDir, "build"));
  const bundle = join(outDir, `${name}.app`);
  log.step(`assembling ${bundle}`);
  rmSync(bundle, { recursive: true, force: true });

  const macos = join(bundle, "Contents", "MacOS");
  const resources = join(bundle, "Contents", "Resources");
  mkdirSync(macos, { recursive: true });
  mkdirSync(resources, { recursive: true });

  copyFileSync(hostBinary, join(macos, "EngawaHost"));
  cpSync(assets, join(resources, "app"), { recursive: true });
  copyFileSync(join(home, "shell-js", "shell.js"), join(resources, "shell.js"));
  copyFileSync(join(appDir, "engawa.json"), join(resources, "engawa.json"));

  // Declared sidecars live under the bundle (contract §7.2); copy the app's bin/ if present.
  if (existsSync(join(appDir, "bin"))) cpSync(join(appDir, "bin"), join(resources, "bin"), { recursive: true });

  // The update trust root (contract §7.1). Present iff the app publishes updates.
  const trustRoot = join(appDir, "trust-root.txt");
  if (existsSync(trustRoot)) copyFileSync(trustRoot, join(resources, "trust-root.txt"));
  else log.warn("no trust-root.txt — the app cannot verify updates (run `engawa keygen`)");

  writeFileSync(join(bundle, "Contents", "Info.plist"), infoPlist(manifest));

  const identity = flags["sign"] ?? "-"; // "-" = ad-hoc
  log.step(`codesigning (${identity === "-" ? "ad-hoc" : identity})`);
  if (!(await toolExists("codesign"))) throw new CliError("codesign not found");
  await run("codesign", ["-s", identity, "--force", bundle]);
  // notarization is out of scope until distribution (a stub hook).

  log.ok(`built ${bundle}`);
  return bundle;
}

export async function cmdBuild(argv: string[]): Promise<void> {
  await buildApp(argv);
}

function escapeXml(s: string): string {
  return s.replace(/[<>&"']/g, (c) => ({ "<": "&lt;", ">": "&gt;", "&": "&amp;", '"': "&quot;", "'": "&apos;" })[c] ?? c);
}

function infoPlist(m: Manifest): string {
  return `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>EngawaHost</string>
  <key>CFBundleIdentifier</key><string>${escapeXml(m.id)}</string>
  <key>CFBundleName</key><string>${escapeXml(appName(m))}</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${escapeXml(m.version)}</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict></plist>
`;
}
