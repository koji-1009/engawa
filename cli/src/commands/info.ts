import { toolExists } from "../exec.ts";
import { log } from "../log.ts";
import { findEngawaHome } from "../paths.ts";

// Contract/host versions and a toolchain check. Kept in sync with the reference host constants.
const CONTRACT_VERSION = "0.1.0"; // DRAFT
const HOST_VERSION = "macos-host-0.1.0";

export async function cmdInfo(): Promise<void> {
  log.info("engawa CLI 0.1.0");

  let home: string;
  try {
    home = findEngawaHome();
  } catch {
    home = "(not found — set ENGAWA_HOME)";
  }
  log.info(`  engawa home:  ${home}`);
  log.info(`  contract:     ${CONTRACT_VERSION} (DRAFT)`);
  log.info(`  host:         ${HOST_VERSION}`);
  log.info("  toolchain:");
  log.info(`    node:       ${process.version}`);
  log.info(`    swift:      ${(await toolExists("swift")) ? "found" : "MISSING"}`);
  log.info(`    codesign:   ${(await toolExists("codesign")) ? "found" : "MISSING"}`);
}
