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

// Is `cmd` on PATH? Uses the platform's own lookup (`where` on Windows, `which` on POSIX resolved via
// PATH — not a hardcoded /usr/bin/which, which is absent on Windows and some distros), so `engawa info`
// reports the toolchain correctly on every OS.
export async function toolExists(cmd: string): Promise<boolean> {
  const probe = process.platform === "win32" ? "where" : "which";
  try {
    await run(probe, [cmd], { quiet: true });
    return true;
  } catch {
    return false;
  }
}
