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
      const key = arg.slice(2);
      const next = argv[i + 1];
      if (next !== undefined && !next.startsWith("--")) {
        flags[key] = next;
        i++;
      } else {
        bools.add(key);
      }
    } else {
      positionals.push(arg);
    }
  }
  return { positionals, flags, bools };
}
