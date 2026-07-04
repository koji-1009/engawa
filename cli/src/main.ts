#!/usr/bin/env node
// engawa — the CLI for building and running Engawa apps. Runs directly on Node 24+ via
// type stripping (no build step); `npm run typecheck` type-checks it.

import { CliError, log } from "./log.ts";
import { cmdBuild } from "./commands/build.ts";
import { cmdDev } from "./commands/dev.ts";
import { cmdInfo } from "./commands/info.ts";
import { cmdKeygen } from "./commands/keygen.ts";
import { cmdNew } from "./commands/new.ts";
import { cmdSign } from "./commands/sign.ts";

const HELP = `engawa — build and run Engawa apps

Usage: engawa <command> [options]

Commands:
  new <name>                     Scaffold a new app (engawa.json + app/)
  dev                            Build (debug) and launch the app
  build [--out <dir>] [--sign <id>]
                                 Bundle a codesigned .app (ad-hoc by default)
  keygen [--out <dir>]           Generate a dev ed25519 signing keypair (§7.1)
  sign <payload> --key <key> [--version <v>]
                                 Sign an app-update payload → {hash, signature}
  info                           Show contract/host versions and toolchain status

Environment:
  ENGAWA_HOME                    Path to the Engawa reference implementation
                                 (auto-detected from the CLI location if unset)`;

async function main(): Promise<void> {
  const [command, ...args] = process.argv.slice(2);

  switch (command) {
    case "new":
      cmdNew(args);
      break;
    case "dev":
      await cmdDev(args);
      break;
    case "build":
      await cmdBuild(args);
      break;
    case "keygen":
      cmdKeygen(args);
      break;
    case "sign":
      cmdSign(args);
      break;
    case "info":
      await cmdInfo();
      break;
    case undefined:
    case "help":
    case "--help":
    case "-h":
      process.stdout.write(`${HELP}\n`);
      break;
    default:
      throw new CliError(`unknown command: ${command}\n\n${HELP}`);
  }
}

main().catch((err: unknown) => {
  if (err instanceof CliError) log.error(err.message);
  else log.error(err instanceof Error ? (err.stack ?? err.message) : String(err));
  process.exitCode = 1;
});
