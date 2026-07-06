// `engawa new` scaffolds an app from a template: files copied verbatim except {{name}}/{{id}}/
// {{sqliteAdapterPath}} placeholders. These tests scaffold each template into a scratch dir and
// assert the tree is complete, engawa.json parses with the right identity, no placeholder survives,
// the sqlite adapter path points at a real directory, --list covers every template, and an unknown
// template is rejected.
import assert from "node:assert/strict";
import { mkdtempSync, readdirSync, readFileSync, rmSync, statSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { after, test } from "node:test";
import { cmdNew } from "../src/commands/new.ts";

const scratchRoots: string[] = [];
function scratch(): string {
  const dir = mkdtempSync(join(tmpdir(), "engawa-new-"));
  scratchRoots.push(dir);
  return dir;
}

// Run cmdNew with cwd set to a fresh scratch dir (cmdNew resolves <name> against process.cwd()),
// restoring cwd afterwards. Returns the generated project directory.
function scaffold(name: string, args: string[]): string {
  const root = scratch();
  const prev = process.cwd();
  process.chdir(root);
  try {
    cmdNew([name, ...args]);
  } finally {
    process.chdir(prev);
  }
  return join(root, name);
}

after(() => {
  for (const dir of scratchRoots) rmSync(dir, { recursive: true, force: true });
});

// Every file under a directory, recursively (absolute paths).
function walk(dir: string): string[] {
  const out: string[] = [];
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const full = join(dir, entry.name);
    if (entry.isDirectory()) out.push(...walk(full));
    else out.push(full);
  }
  return out;
}

const TEMPLATES = ["minimal", "static", "sqlite", "updatable"];

for (const template of TEMPLATES) {
  test(`scaffolds the ${template} template`, () => {
    const dir = scaffold("MyApp", ["--template", template]);

    // Baseline files every template produces.
    for (const rel of ["engawa.json", "README.md", ".gitignore", "app/index.html"]) {
      assert.ok(statSync(join(dir, ...rel.split("/"))).isFile(), `${rel} should exist`);
    }

    // engawa.json parses and carries the derived identity + name.
    const manifest = JSON.parse(readFileSync(join(dir, "engawa.json"), "utf8"));
    assert.equal(manifest.id, "dev.engawa.myapp");
    assert.equal(manifest.name, "MyApp");
    assert.equal(manifest.version, "1.0.0");

    // No placeholder may survive in any generated text file.
    for (const file of walk(dir)) {
      const contents = readFileSync(file, "utf8");
      assert.ok(!contents.includes("{{"), `${file} still contains a {{ placeholder`);
    }
  });
}

test("sqlite template points its adapter path at a real directory", () => {
  const dir = scaffold("SqlApp", ["--template", "sqlite"]);
  const manifest = JSON.parse(readFileSync(join(dir, "engawa.json"), "utf8"));
  const adapterPath = manifest.adapters[0].path;
  assert.ok(typeof adapterPath === "string" && adapterPath.length > 0, "adapter path should be set");
  assert.ok(statSync(adapterPath).isDirectory(), `adapter path ${adapterPath} should be an existing directory`);
});

test("the default template is minimal (no --template flag)", () => {
  const dir = scaffold("DefaultApp", []);
  const manifest = JSON.parse(readFileSync(join(dir, "engawa.json"), "utf8"));
  assert.deepEqual(manifest.adapters, []);
  assert.ok(statSync(join(dir, "app", "main.js")).isFile());
});

test("--list covers every template", () => {
  const lines: string[] = [];
  const orig = process.stderr.write.bind(process.stderr);
  (process.stderr as { write: unknown }).write = (chunk: string | Uint8Array): boolean => {
    lines.push(chunk.toString());
    return true;
  };
  try {
    cmdNew(["--list"]);
  } finally {
    (process.stderr as { write: unknown }).write = orig;
  }
  const output = lines.join("");
  for (const template of TEMPLATES) {
    assert.match(output, new RegExp(`\\b${template}\\b`), `--list should mention ${template}`);
  }
  assert.match(output, /\(default\)/);
});

test("an unknown template is rejected, listing the available ones", () => {
  assert.throws(
    () => scaffold("BadApp", ["--template", "does-not-exist"]),
    (err: Error) => {
      assert.match(err.message, /unknown template/);
      assert.match(err.message, /minimal/);
      return true;
    },
  );
});
