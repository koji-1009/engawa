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

// A minimal notes app over the sqlite adapter — durable data (contract §10). External script
// only (inline script is dead under the default CSP, §7.3).
const MAIN_JS = `'use strict';
(function () {
  var engawa = window.engawa;
  async function openDB() {
    var base = await engawa.invoke('path.appData');
    var db = (await engawa.invoke('sqlite.open', { path: base + '/app.db' })).db;
    await engawa.invoke('sqlite.execute', { db: db, sql: 'CREATE TABLE IF NOT EXISTS notes(id INTEGER PRIMARY KEY, body TEXT)' });
    return db;
  }
  async function render() {
    var db = await openDB();
    var rows = (await engawa.invoke('sqlite.query', { db: db, sql: 'SELECT body FROM notes ORDER BY id' })).rows;
    await engawa.invoke('sqlite.close', { db: db });
    var ul = document.getElementById('notes');
    ul.textContent = '';
    rows.forEach(function (r) { var li = document.createElement('li'); li.textContent = r.body; ul.appendChild(li); });
  }
  document.getElementById('add').addEventListener('submit', async function (ev) {
    ev.preventDefault();
    var body = document.getElementById('body').value.trim();
    if (!body) return;
    var db = await openDB();
    await engawa.invoke('sqlite.execute', { db: db, sql: 'INSERT INTO notes(body) VALUES(?)', params: [body] });
    await engawa.invoke('sqlite.close', { db: db });
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
  const manifest = { id: `dev.engawa.${slug}`, name: basename(name), version: "1.0.0", sidecars: [] as string[] };

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
