'use strict';
// Node mock host (contract §11). It implements the two host primitives —
// receive a string (`postMessage`) and evaluate a string (`_deliver`) — over
// in-process JS command handlers, and runs the real shell.js (identical bytes)
// in a vm context. If a requirement needs engine emulation to pass here, that
// requirement is an engine-ism smuggled into the contract — a spec bug.

const vm = require('vm');
const fs = require('fs');
const os = require('os');
const path = require('path');
const child_process = require('child_process');
const nodeSqlite = require('node:sqlite');

const SHELL_JS = fs.readFileSync(
  path.join(__dirname, '..', '..', '..', 'shell-js', 'shell.js'), 'utf8');

// The conformance app bundle: manifest + sidecar allowlist (contract §7.2), shared with
// the macOS host via ENGAWA_BUNDLE_ROOT.
const BUNDLE_ROOT = path.join(__dirname, '..', '..', 'fixtures', 'bundle');
let MANIFEST = { sidecars: [] };
try { MANIFEST = JSON.parse(fs.readFileSync(path.join(BUNDLE_ROOT, 'engawa.json'), 'utf8')); } catch { /* no bundle */ }
const PROC_CAP = 8 * 1024 * 1024;

function resolveSidecar(command) {
  if (!Array.isArray(MANIFEST.sidecars) || !MANIFEST.sidecars.includes(command)) return null;
  const resolved = path.resolve(BUNDLE_ROOT, command);
  if (resolved !== BUNDLE_ROOT && !resolved.startsWith(BUNDLE_ROOT + path.sep)) return null;
  return resolved;
}

// Longest byte length ≤ take that does not split a UTF-8 sequence.
function validUtf8End(buf, take) {
  let end = take;
  while (end > 0 && end < buf.length && (buf[end] & 0xc0) === 0x80) end--;
  return end;
}

// Build the command handlers, keyed by full `namespace.command`. `ctx.emitEvent(topic,
// payload)` lets a handler raise an event frame (contract §2). `echo` remains as the
// slice fixture; §4 namespaces are added here as they land.
function buildHandlers(ctx) {
  const dataRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'engawa-mock-'));
  const ensure = (p) => { fs.mkdirSync(p, { recursive: true }); return p; };
  const clipboard = { text: '' };

  const win = { width: 1024, height: 720, title: 'Engawa', resizable: true, minimized: false, maximized: false };
  const closeTokens = new Set();
  let tokenSeq = 0;

  const shellOpenRecorded = [];
  const notificationsRecorded = [];
  const dialog = { next: null };
  const sqdbs = new Map();
  let sqNext = 1;

  // process (spec/commands/process.md) — pull streams over child_process
  const procs = new Map();
  const newStream = () => ({ buffer: Buffer.alloc(0), eof: false, paused: false, source: null });
  function wireStream(pid, st, which, src) {
    const stream = st[which];
    stream.source = src;
    src.on('data', (chunk) => {
      const wasEmpty = stream.buffer.length === 0;
      stream.buffer = Buffer.concat([stream.buffer, chunk]);
      if (stream.buffer.length >= PROC_CAP) { stream.paused = true; src.pause(); }
      if (wasEmpty) ctx.emitEvent('process.readable', { pid, stream: which });
    });
    src.on('end', () => { stream.eof = true; maybeProcExit(pid); });
  }
  function maybeProcExit(pid) {
    const st = procs.get(pid);
    if (!st) return;
    if (st.exited && !st.exitEmitted && st.stdout.eof && st.stderr.eof
        && st.stdout.buffer.length === 0 && st.stderr.buffer.length === 0) {
      st.exitEmitted = true;
      ctx.emitEvent('process.exit', { pid, code: st.exitCode });
    }
  }

  return {
    echo: async (args) => args,

    // path (spec/commands/path.md)
    'path.appData': async () => ensure(path.join(dataRoot, 'data')),
    'path.appConfig': async () => ensure(path.join(dataRoot, 'config')),
    'path.appCache': async () => ensure(path.join(dataRoot, 'cache')),
    'path.home': async () => os.homedir(),
    'path.temp': async () => os.tmpdir(),

    // fs (spec/commands/fs.md) — text only; mirrors the macOS adapter's semantics/codes.
    'fs.readTextFile': async (a) => {
      const p = reqPath(a);
      let st;
      try { st = fs.statSync(p); } catch { throw err('ENOENT', 'no such file: ' + p); }
      if (st.isDirectory()) throw err('EISDIR', 'is a directory: ' + p);
      try { return fs.readFileSync(p, 'utf8'); } catch (e) { throw err('EIO', e.message); }
    },
    'fs.writeTextFile': async (a) => {
      const p = reqPath(a);
      if (!a || typeof a.contents !== 'string') throw err('EINVAL', 'contents required');
      const parent = path.dirname(p);
      let pst;
      try { pst = fs.statSync(parent); } catch { throw err('ENOENT', 'parent directory does not exist: ' + parent); }
      if (!pst.isDirectory()) throw err('ENOENT', 'parent directory does not exist: ' + parent);
      const tmp = p + '.engawa-tmp';
      try { fs.writeFileSync(tmp, a.contents, 'utf8'); fs.renameSync(tmp, p); return null; }
      catch (e) { try { fs.rmSync(tmp, { force: true }); } catch {} throw err('EIO', e.message); }
    },
    'fs.exists': async (a) => fs.existsSync(reqPath(a)),
    'fs.mkdir': async (a) => {
      const p = reqPath(a);
      const recursive = !!(a && a.recursive);
      if (fs.existsSync(p)) { if (recursive) return null; throw err('EEXIST', 'already exists: ' + p); }
      try { fs.mkdirSync(p, { recursive }); return null; } catch (e) { throw err('EIO', e.message); }
    },
    'fs.remove': async (a) => {
      const p = reqPath(a);
      const recursive = !!(a && a.recursive);
      let st;
      try { st = fs.lstatSync(p); } catch { throw err('ENOENT', 'no such path: ' + p); }
      if (st.isDirectory() && !recursive && fs.readdirSync(p).length) throw err('ENOTEMPTY', 'directory not empty: ' + p);
      try { fs.rmSync(p, { recursive }); return null; } catch (e) { throw err('EIO', e.message); }
    },
    'fs.readDir': async (a) => {
      const p = reqPath(a);
      let st;
      try { st = fs.statSync(p); } catch { throw err('ENOENT', 'no such path: ' + p); }
      if (!st.isDirectory()) throw err('ENOTDIR', 'not a directory: ' + p);
      return fs.readdirSync(p).map((name) => {
        let isDirectory = false;
        try { isDirectory = fs.statSync(path.join(p, name)).isDirectory(); } catch { /* raced away */ }
        return { name, isDirectory };
      });
    },
    'fs.stat': async (a) => {
      const p = reqPath(a);
      let st;
      try { st = fs.statSync(p); } catch { throw err('ENOENT', 'no such path: ' + p); }
      return { type: st.isDirectory() ? 'directory' : 'file', size: st.size, modified: st.mtimeMs };
    },

    // app (spec/commands/app.md)
    'app.version': async () => '0.0.0',
    'app.engineInfo': async () => ({
      engine: 'mock',
      engineVersion: process.version,
      hostVersion: 'mock-0.1',
      contractVersion: '0.1.0',
    }),
    'app.quit': async () => null,   // the mock host has no process to end

    // clipboard (spec/commands/clipboard.md) — in-memory, per host instance
    'clipboard.writeText': async (a) => {
      if (!a || typeof a.text !== 'string') throw err('EINVAL', 'text required');
      clipboard.text = a.text;
      return null;
    },
    'clipboard.readText': async () => clipboard.text,

    // window (spec/commands/window.md) — in-memory window model
    'window.setTitle': async (a) => { win.title = String((a && a.title) || ''); return null; },
    'window.getSize': async () => ({ width: win.width, height: win.height }),
    'window.setSize': async (a) => {
      if (!a || typeof a.width !== 'number' || typeof a.height !== 'number') throw err('EINVAL', 'width/height required');
      win.width = a.width; win.height = a.height;
      ctx.emitEvent('window.resize', { width: win.width, height: win.height });
      return null;
    },
    // Conformance hook: fire many resizes in one tick so the suite can observe §2.1 coalescing.
    'window.__resizeStorm': async (a) => {
      const count = a && typeof a.count === 'number' ? a.count : 8;
      const from = a && typeof a.from === 'number' ? a.from : 300;
      for (let i = 0; i < count; i++) ctx.emitEvent('window.resize', { width: from + i, height: from + i });
      return { from: from, count: count, last: from + count - 1 };
    },
    'window.setResizable': async (a) => { win.resizable = !!(a && a.resizable); return null; },
    'window.minimize': async () => { win.minimized = true; return null; },
    'window.maximize': async () => { win.maximized = true; return null; },
    'window.close': async () => null,   // mock has no real window to destroy
    'window.respondToClose': async (a) => {
      const token = a && a.token;
      if (typeof token !== 'number' || !closeTokens.has(token)) throw err('EINVAL', 'unknown or consumed close token');
      closeTokens.delete(token);
      return null;
    },
    // Conformance testability hook (spec/commands/window.md): simulate a user close attempt.
    'window.requestClose': async () => {
      tokenSeq += 1;
      const token = tokenSeq;
      closeTokens.add(token);
      ctx.emitEvent('window.closeRequested', { token });
      return null;
    },

    // shellOpen (spec/commands/shellOpen.md) — record-only (the mock has no OS to hand off to)
    'shellOpen.openExternal': async (a) => {
      if (!a || typeof a.url !== 'string' || !a.url) throw err('EINVAL', 'url required');
      shellOpenRecorded.push({ action: 'openExternal', url: a.url });
      return null;
    },
    'shellOpen.revealInFolder': async (a) => {
      if (!a || typeof a.path !== 'string' || !a.path) throw err('EINVAL', 'path required');
      shellOpenRecorded.push({ action: 'revealInFolder', path: a.path });
      return null;
    },
    'shellOpen.__recorded': async () => shellOpenRecorded.slice(),

    // notification (spec/commands/notification.md) — record-only
    'notification.show': async (a) => {
      if (!a || typeof a.title !== 'string' || !a.title) throw err('EINVAL', 'title required');
      notificationsRecorded.push({ title: a.title, body: typeof a.body === 'string' ? a.body : '' });
      return null;
    },
    'notification.__recorded': async () => notificationsRecorded.slice(),

    // process (spec/commands/process.md)
    'process.spawn': async (a) => {
      const command = a && a.command;
      if (typeof command !== 'string' || !command) throw err('EINVAL', 'command required');
      const exe = resolveSidecar(command);
      if (!exe) throw err('EPERM', 'not a declared in-bundle sidecar: ' + command);
      const args = a.args && Array.isArray(a.args) ? a.args.filter((x) => typeof x === 'string') : [];
      const proc = child_process.spawn(exe, args, { stdio: ['pipe', 'pipe', 'pipe'] });
      const pid = proc.pid;
      const st = { proc, stdout: newStream(), stderr: newStream(), exited: false, exitCode: 0, exitEmitted: false };
      procs.set(pid, st);
      wireStream(pid, st, 'stdout', proc.stdout);
      wireStream(pid, st, 'stderr', proc.stderr);
      proc.on('exit', (code) => { st.exited = true; st.exitCode = code == null ? -1 : code; maybeProcExit(pid); });
      return { pid };
    },
    'process.stdinWrite': async (a) => {
      if (!a || typeof a.pid !== 'number') throw err('EINVAL', 'pid required');
      if (typeof a.data !== 'string') throw err('EINVAL', 'data required');
      const st = procs.get(a.pid);
      if (!st) throw err('ESRCH', 'no such process: ' + a.pid);
      st.proc.stdin.write(Buffer.from(a.data, 'utf8'));
      return null;
    },
    'process.read': async (a) => {
      if (!a || typeof a.pid !== 'number') throw err('EINVAL', 'pid required');
      const st = procs.get(a.pid);
      if (!st) throw err('ESRCH', 'no such process: ' + a.pid);
      const which = a.stream === 'stderr' ? 'stderr' : 'stdout';
      const stream = st[which];
      const maxBytes = typeof a.maxBytes === 'number' ? a.maxBytes : 65536;
      const take = Math.min(Math.max(maxBytes, 0), stream.buffer.length);
      const end = validUtf8End(stream.buffer, take);
      const data = stream.buffer.toString('utf8', 0, end);
      stream.buffer = stream.buffer.subarray(end);
      if (stream.paused && !stream.eof && stream.buffer.length < PROC_CAP && stream.source) {
        stream.paused = false; stream.source.resume();
      }
      const eof = stream.eof && stream.buffer.length === 0;
      maybeProcExit(a.pid);
      return { data, eof };
    },
    'process.kill': async (a) => {
      if (!a || typeof a.pid !== 'number') throw err('EINVAL', 'pid required');
      const st = procs.get(a.pid);
      if (!st) throw err('ESRCH', 'no such process: ' + a.pid);
      st.proc.kill('SIGTERM');
      return null;
    },

    // dialog (spec/commands/dialog.md) — preprogrammed responses under conformance
    'dialog.open': async () => takeDialog({ canceled: true, paths: [] }),
    'dialog.save': async () => takeDialog({ canceled: true, path: null }),
    'dialog.message': async (a) => {
      if (!a || typeof a.message !== 'string') throw err('EINVAL', 'message required');
      return takeDialog({ button: 0 });
    },
    'dialog.__setResponse': async (a) => { dialog.next = a; return null; },

    // sqlite (adapters/sqlite/spec.md) — via node:sqlite; the same namespace the macOS adapter serves
    'sqlite.open': async (a) => {
      if (!a || typeof a.path !== 'string' || !a.path) throw err('EINVAL', 'path required');
      let db;
      try { db = new nodeSqlite.DatabaseSync(a.path); } catch (e) { throw err('ESQLITE', e.message); }
      const handle = sqNext++;
      sqdbs.set(handle, db);
      return { db: handle };
    },
    'sqlite.execute': async (a) => {
      const db = sqdbs.get(a && a.db);
      if (!db) throw err('EBADF', 'unknown db handle');
      if (typeof a.sql !== 'string') throw err('EINVAL', 'sql required');
      try {
        const r = db.prepare(a.sql).run(...sqliteParams(a.params));
        return { changes: Number(r.changes), lastInsertRowid: Number(r.lastInsertRowid) };
      } catch (e) { throw err('ESQLITE', e.message); }
    },
    'sqlite.query': async (a) => {
      const db = sqdbs.get(a && a.db);
      if (!db) throw err('EBADF', 'unknown db handle');
      if (typeof a.sql !== 'string') throw err('EINVAL', 'sql required');
      try {
        return { rows: db.prepare(a.sql).all(...sqliteParams(a.params)).map(sqliteRow) };
      } catch (e) { throw err('ESQLITE', e.message); }
    },
    'sqlite.close': async (a) => {
      const db = sqdbs.get(a && a.db);
      if (!db) throw err('EBADF', 'unknown db handle');
      db.close();
      sqdbs.delete(a.db);
      return null;
    },
  };

  function takeDialog(fallback) {
    const r = dialog.next === null ? fallback : dialog.next;
    dialog.next = null;
    return r;
  }
}

function err(code, message) {
  const e = new Error(message);
  e.code = code;
  return e;
}

// §2.1: keep only the latest window.resize event in a delivery batch.
function coalesceResize(frames) {
  let last = -1;
  for (let i = 0; i < frames.length; i++) {
    if (frames[i].t === 'evt' && frames[i].topic === 'window.resize') last = i;
  }
  if (last < 0) return frames;
  return frames.filter((f, i) => !(f.t === 'evt' && f.topic === 'window.resize' && i !== last));
}

function reqPath(a) {
  if (!a || typeof a.path !== 'string' || a.path.length === 0) throw err('EINVAL', 'path required');
  return a.path;
}

// Booleans bind as 0/1 (SQLite has no boolean type) — matches the macOS adapter.
function sqliteParams(params) {
  if (!Array.isArray(params)) return [];
  return params.map((p) => (typeof p === 'boolean' ? (p ? 1 : 0) : p));
}

// node:sqlite returns null-prototype rows and BigInt integers; normalize to plain JSON.
function sqliteRow(row) {
  const out = {};
  for (const k of Object.keys(row)) {
    const v = row[k];
    out[k] = typeof v === 'bigint' ? Number(v) : v;
  }
  return out;
}

// Build one runtime instance backed by in-process handlers.
function createMockHost(options = {}) {
  let outbound = [];         // frames queued for delivery
  let scheduled = false;

  const sandbox = { console };
  sandbox.globalThis = sandbox;
  const context = vm.createContext(sandbox);

  // Flow control (contract §2.1): drain the outbound queue into at most one
  // `_deliver` eval per tick, frames batched into a single JSON array.
  function scheduleFlush() {
    if (scheduled) return;
    scheduled = true;
    queueMicrotask(() => {
      scheduled = false;
      if (outbound.length === 0) return;
      const batch = coalesceResize(outbound);   // §2.1: latest window.resize per batch only
      outbound = [];
      shell._deliver(JSON.stringify(batch));
    });
  }

  function emit(frame) { outbound.push(frame); scheduleFlush(); }
  function emitEvent(topic, payload) { emit({ t: 'evt', topic, payload }); }

  const handlers = Object.assign(buildHandlers({ emitEvent }), options.handlers || {});
  const capabilities = options.capabilities ||
    [...new Set(Object.keys(handlers).map((k) => k.split('.')[0]))];

  const shell = {
    contractVersion: '0.1.0',
    platform: 'mock',
    capabilities,
    postMessage(jsonString) { onRequest(jsonString); },
  };
  sandbox.__shell = shell;

  vm.runInContext(SHELL_JS, context, { filename: 'shell.js' });

  async function onRequest(jsonString) {
    let frame;
    try { frame = JSON.parse(jsonString); } catch { return; }
    if (!frame || frame.t !== 'req') return;
    const handler = handlers[frame.cmd];
    if (!handler) {
      emit({ t: 'res', id: frame.id, ok: false, err: { code: 'ENOSYS', message: `unknown command: ${frame.cmd}` } });
      return;
    }
    try {
      const value = await handler(frame.args);
      emit({ t: 'res', id: frame.id, ok: true, value: value === undefined ? null : value });
    } catch (e) {
      emit({ t: 'res', id: frame.id, ok: false, err: { code: e.code || 'EUNKNOWN', message: e.message || String(e) } });
    }
  }

  return {
    name: 'mock',
    engawa: context.engawa,
    emitEvent,
  };
}

module.exports = { createMockHost };
