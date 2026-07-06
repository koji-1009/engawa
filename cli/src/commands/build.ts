import { chmodSync, copyFileSync, cpSync, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { parseArgs } from "../args.ts";
import { toolExists, run } from "../exec.ts";
import { buildHost } from "../host.ts";
import { CliError, log } from "../log.ts";
import { appName, readManifest, type Manifest } from "../manifest.ts";
import { findEngawaHome } from "../paths.ts";

export interface BuildOptions {
  dev?: boolean; // debug config + skip release optimizations
}

// Build the app into a distributable and return its path (a codesigned .app on macOS; a plain
// runnable folder on Windows). Per-environment: a Windows build never needs the macOS toolchain.
export async function buildApp(argv: string[], options: BuildOptions = {}): Promise<string> {
  if (process.platform === "win32") return assembleNativeApp(argv, options, "windows");
  if (process.platform === "linux") return assembleNativeApp(argv, options, "linux");

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

  // Static composition (design.md): a host with only this app's declared adapters.
  const hostBinary = await buildHost(home, appDir, manifest, config);

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

// Windows/Linux: no .app, no codesign. Build the per-app native host, then assemble a plain runnable
// folder (EngawaHost + app/ + shell.js + engawa.json beside it). The host's HostOptions defaults
// resolve its assets from the exe's own directory, so launching it just works — no environment needed.
// The two platforms differ only in the executable's filename and Linux needing the exec bit set.
async function assembleNativeApp(argv: string[], options: BuildOptions, platform: "windows" | "linux"): Promise<string> {
  const { flags } = parseArgs(argv);
  const appDir = resolve(flags["dir"] ?? ".");
  const manifest = readManifest(appDir);
  const assets = join(appDir, "app");
  if (!existsSync(join(assets, "index.html"))) {
    throw new CliError(`${assets}/index.html not found — an Engawa app serves its assets from app/`);
  }

  const home = findEngawaHome();
  const config = options.dev === true ? "debug" : "release";

  // The update trust root (§7.1) is COMPILED INTO the host — not shipped as a swappable file beside
  // the exe. Read it here and hand it to the host build; the generated Compose bakes it in.
  const trustRootPath = join(appDir, "trust-root.txt"); // present iff the app publishes updates
  const trustRoot = existsSync(trustRootPath) ? readFileSync(trustRootPath, "utf8").trim() : undefined;
  if (trustRoot === undefined) log.warn("no trust-root.txt — the app cannot verify updates (run `engawa keygen`)");

  const hostBinary = await buildHost(home, appDir, manifest, config, trustRoot);

  const name = appName(manifest);
  const outDir = resolve(flags["out"] ?? join(appDir, "build"));
  const bundle = join(outDir, name);
  log.step(`assembling ${bundle}`);
  rmSync(bundle, { recursive: true, force: true });
  mkdirSync(bundle, { recursive: true });

  const exe = join(bundle, platform === "windows" ? `${name}.exe` : name);
  copyFileSync(hostBinary, exe);
  if (platform === "linux") chmodSync(exe, 0o755); // copyFileSync does not preserve the executable bit
  cpSync(assets, join(bundle, "app"), { recursive: true });
  copyFileSync(join(home, "shell-js", "shell.js"), join(bundle, "shell.js"));
  copyFileSync(join(appDir, "engawa.json"), join(bundle, "engawa.json"));
  if (existsSync(join(appDir, "bin"))) cpSync(join(appDir, "bin"), join(bundle, "bin"), { recursive: true });

  // Authenticode/ELF signing is out of scope until distribution (matches the macOS ad-hoc/notarization stub).
  log.ok(`built ${bundle}`);
  return bundle;
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
