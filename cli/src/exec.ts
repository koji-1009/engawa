import { spawn } from "node:child_process";

export interface RunOptions {
  cwd?: string;
  env?: NodeJS.ProcessEnv;
  quiet?: boolean; // suppress the child's stdout (stderr still inherited)
}

// Run a subprocess to completion; rejects on non-zero exit.
export function run(cmd: string, args: string[], opts: RunOptions = {}): Promise<void> {
  return new Promise((resolve, reject) => {
    const child = spawn(cmd, args, {
      cwd: opts.cwd,
      env: opts.env ?? process.env,
      stdio: opts.quiet === true ? ["ignore", "ignore", "inherit"] : "inherit",
    });
    child.on("error", reject);
    child.on("exit", (code, signal) => {
      if (code === 0) resolve();
      else reject(new Error(`\`${cmd} ${args.join(" ")}\` failed (${code !== null ? `exit ${code}` : `signal ${signal}`})`));
    });
  });
}

export async function toolExists(cmd: string): Promise<boolean> {
  try {
    await run("/usr/bin/which", [cmd], { quiet: true });
    return true;
  } catch {
    return false;
  }
}
