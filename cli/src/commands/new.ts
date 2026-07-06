import { existsSync, mkdirSync, readdirSync, readFileSync, writeFileSync } from "node:fs";
import { basename, dirname, join, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs } from "../args.ts";
import { CliError, log } from "../log.ts";
import { findEngawaHome } from "../paths.ts";

// Templates ship beside the CLI source (package.json "files" includes "templates"): the layout is
// cli/src/commands/new.ts → cli/templates/<name>/. Resolve relative to this module so it works both
// from a checkout and an installed package.
const TEMPLATES_DIR = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..", "templates");

const DEFAULT_TEMPLATE = "minimal";

// Files copied verbatim except these placeholders, substituted in every text file. {{name}} and
// {{id}} come from the app name; {{sqliteAdapterPath}} is the absolute dev-time path to the repo's
// adapters/sqlite (resolved via findEngawaHome), only present in the sqlite template.
interface Substitutions {
  name: string;
  id: string;
  sqliteAdapterPath: string;
}

function applySubstitutions(text: string, subs: Substitutions): string {
  return text
    .replaceAll("{{name}}", subs.name)
    .replaceAll("{{id}}", subs.id)
    .replaceAll("{{sqliteAdapterPath}}", subs.sqliteAdapterPath);
}

function listTemplates(): string[] {
  if (!existsSync(TEMPLATES_DIR)) throw new CliError(`templates directory not found at ${TEMPLATES_DIR}`);
  return readdirSync(TEMPLATES_DIR, { withFileTypes: true })
    .filter((e) => e.isDirectory())
    .map((e) => e.name)
    .sort();
}

// One-line template description = the README.md first heading line, minus the leading "# " and
// the "{{name}} — " project-name prefix the templates use (so `--list` reads cleanly).
function templateDescription(template: string): string {
  try {
    const readme = readFileSync(join(TEMPLATES_DIR, template, "README.md"), "utf8");
    const first = readme.split("\n").find((l) => l.startsWith("# "));
    if (first) return first.slice(2).replace(/^\{\{name\}\}\s*[—-]\s*/, "").trim();
  } catch {
    /* fall through */
  }
  return "(no description)";
}

// Recursively copy a template tree into `dest`, substituting placeholders in every file.
function copyTree(srcDir: string, destDir: string, subs: Substitutions): void {
  mkdirSync(destDir, { recursive: true });
  for (const entry of readdirSync(srcDir, { withFileTypes: true })) {
    const src = join(srcDir, entry.name);
    const dest = join(destDir, entry.name);
    if (entry.isDirectory()) {
      copyTree(src, dest, subs);
    } else {
      writeFileSync(dest, applySubstitutions(readFileSync(src, "utf8"), subs));
    }
  }
}

// The sqlite template needs an absolute, JSON-safe path to the repo's adapters/sqlite. Only
// resolve it (which requires an Engawa home) when the chosen template actually references it. The
// placeholder sits inside a JSON string literal, and on Windows the path has backslashes, so emit
// it JSON-escaped (JSON.stringify then strip the surrounding quotes).
function sqliteAdapterPath(templateDir: string): string {
  const needsIt = readFileSync(join(templateDir, "engawa.json"), "utf8").includes("{{sqliteAdapterPath}}");
  if (!needsIt) return "";
  const abs = join(findEngawaHome(), "adapters", "sqlite");
  return JSON.stringify(abs).slice(1, -1);
}

export function cmdNew(argv: string[]): void {
  const { positionals, flags, bools } = parseArgs(argv);

  if (bools.has("list")) {
    const templates = listTemplates();
    log.info("Available templates:");
    const width = Math.max(...templates.map((t) => t.length));
    for (const t of templates) {
      const marker = t === DEFAULT_TEMPLATE ? " (default)" : "";
      log.info(`  ${t.padEnd(width)}  ${templateDescription(t)}${marker}`);
    }
    return;
  }

  const name = positionals[0];
  if (name === undefined) throw new CliError("usage: engawa new <name> [--template <t>]  (see `engawa new --list`)");

  const template = flags["template"] ?? DEFAULT_TEMPLATE;
  const templateDir = join(TEMPLATES_DIR, template);
  if (!existsSync(templateDir)) {
    throw new CliError(`unknown template "${template}" — available: ${listTemplates().join(", ")}`);
  }

  const dir = resolve(name);
  if (existsSync(dir)) throw new CliError(`${dir} already exists`);

  const slug = basename(name).replace(/[^a-z0-9]+/gi, "").toLowerCase() || "app";
  const subs: Substitutions = {
    name: basename(name),
    id: `dev.engawa.${slug}`,
    sqliteAdapterPath: sqliteAdapterPath(templateDir),
  };

  copyTree(templateDir, dir, subs);
  writeFileSync(join(dir, ".gitignore"), "build/\n*.app\nengawa-signing.key\ntrust-root.txt\n");

  log.ok(`created ${name}/ from the ${template} template`);
  log.info(`  cd ${name}`);
  log.info(`  engawa dev        # build + run`);
  // Platform-aware: build produces a runnable folder on Windows/Linux, a .app bundle on macOS.
  const built = process.platform === "darwin" ? "a runnable .app bundle" : "a runnable app folder";
  log.info(`  engawa build      # bundle ${built}`);
}
