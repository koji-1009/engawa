import { join } from "node:path";
import { run } from "../exec.ts";
import { log } from "../log.ts";
import { buildApp } from "./build.ts";

// Build (debug) and launch the app in the foreground; Ctrl-C to quit.
export async function cmdDev(argv: string[]): Promise<void> {
  const bundle = await buildApp(argv, { dev: true });
  log.step(`launching ${bundle}`);
  await run(join(bundle, "Contents", "MacOS", "EngawaHost"), []);
}
