// Diagnostics go to stderr; machine-readable output (e.g. `sign`) goes to stdout.

const useColor = process.stdout.isTTY === true && process.env["NO_COLOR"] === undefined;
const paint = (code: string, s: string): string => (useColor ? `\x1b[${code}m${s}\x1b[0m` : s);

export const log = {
  info: (msg: string): void => void process.stderr.write(`${msg}\n`),
  step: (msg: string): void => void process.stderr.write(`${paint("36", "›")} ${msg}\n`),
  ok: (msg: string): void => void process.stderr.write(`${paint("32", "✓")} ${msg}\n`),
  warn: (msg: string): void => void process.stderr.write(`${paint("33", "!")} ${msg}\n`),
  error: (msg: string): void => void process.stderr.write(`${paint("31", "✗")} ${msg}\n`),
};

// A CliError is a user-facing failure: printed as a clean message, no stack trace.
export class CliError extends Error {}
