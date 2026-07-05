// Minimal, dependency-free flag parser: `--key value` sets a flag, a bare `--flag`
// (or `--flag` followed by another `--…`) is a boolean, everything else is positional.

export interface Args {
  positionals: string[];
  flags: Record<string, string>;
  bools: Set<string>;
}

export function parseArgs(argv: string[]): Args {
  const positionals: string[] = [];
  const flags: Record<string, string> = {};
  const bools = new Set<string>();

  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i]!;
    if (arg.startsWith("--")) {
      const body = arg.slice(2);
      const eq = body.indexOf("=");
      if (eq >= 0) {
        flags[body.slice(0, eq)] = body.slice(eq + 1); // --key=value
        continue;
      }
      const next = argv[i + 1];
      if (next !== undefined && !next.startsWith("--")) {
        flags[body] = next; // --key value
        i++;
      } else {
        bools.add(body); // --flag
      }
    } else {
      positionals.push(arg);
    }
  }
  return { positionals, flags, bools };
}
