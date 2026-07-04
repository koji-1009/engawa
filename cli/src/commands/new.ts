import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { basename, join, resolve } from "node:path";
import { parseArgs } from "../args.ts";
import { CliError, log } from "../log.ts";

const INDEX_HTML = `<!doctype html>
<meta charset="utf-8">
<meta name="app-version" content="1">
<title>Engawa App</title>
<h1>Engawa App</h1>
<ul id="notes"></ul>
<form id="add"><input id="body" placeholder="a note" autocomplete="off"><button>add</button></form>
<script src="main.js"></script>
`;

// A minimal notes app backed by the built-in fs namespace — durable data (contract §10), no
// adapters needed, so it builds and runs out of the box. To use sqlite instead, declare the
// adapter in engawa.json's "adapters" (see examples/notes). External script only (inline script
// is dead under the default CSP, §7.3).
const MAIN_JS = `'use strict';
(function () {
  var engawa = window.engawa;
  var file;
  async function load() {
    if (!file) file = (await engawa.invoke('path.appData')) + '/notes.json';
    try { return JSON.parse(await engawa.invoke('fs.readTextFile', { path: file })); }
    catch (e) { return []; }
  }
  async function save(notes) { await engawa.invoke('fs.writeTextFile', { path: file, contents: JSON.stringify(notes) }); }
  async function render() {
    var notes = await load();
    var ul = document.getElementById('notes');
    ul.textContent = '';
    notes.forEach(function (n) { var li = document.createElement('li'); li.textContent = n; ul.appendChild(li); });
  }
  document.getElementById('add').addEventListener('submit', async function (ev) {
    ev.preventDefault();
    var body = document.getElementById('body').value.trim();
    if (!body) return;
    var notes = await load();
    notes.push(body);
    await save(notes);
    document.getElementById('body').value = '';
    render();
  });
  render();
})();
`;

export function cmdNew(argv: string[]): void {
  const { positionals } = parseArgs(argv);
  const name = positionals[0];
  if (name === undefined) throw new CliError("usage: engawa new <name>");

  const dir = resolve(name);
  if (existsSync(dir)) throw new CliError(`${dir} already exists`);

  const slug = basename(name).replace(/[^a-z0-9]+/gi, "").toLowerCase() || "app";
  const manifest = { id: `dev.engawa.${slug}`, name: basename(name), version: "1.0.0", adapters: [] as unknown[], sidecars: [] as string[] };

  mkdirSync(join(dir, "app"), { recursive: true });
  writeFileSync(join(dir, "engawa.json"), JSON.stringify(manifest, null, 2) + "\n");
  writeFileSync(join(dir, "app", "index.html"), INDEX_HTML);
  writeFileSync(join(dir, "app", "main.js"), MAIN_JS);
  writeFileSync(join(dir, ".gitignore"), "build/\n*.app\nengawa-signing.key\ntrust-root.txt\n");

  log.ok(`created ${name}/`);
  log.info(`  cd ${name}`);
  log.info(`  engawa dev        # build + run`);
  log.info(`  engawa build      # bundle a .app`);
}
