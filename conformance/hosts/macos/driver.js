'use strict';
// macOS reference-host driver for the conformance runner.
//
// Launches the Swift host in conformance mode and exposes an `engawa`-shaped proxy.
// Each invoke() is forwarded over a stdio control channel to the host, which runs it
// through the REAL in-page shell.js (round-trip to native dispatch) and reports back.
// So the same suite that runs against the mock host here exercises the full live stack.

const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');
const os = require('os');

const REPO = path.join(__dirname, '..', '..', '..');
const HOST_BIN = path.join(REPO, 'hosts', 'macos', '.build', 'debug', 'EngawaHost');
const SHELL_JS = path.join(REPO, 'shell-js', 'shell.js');
const BUNDLE_ROOT = path.join(REPO, 'conformance', 'fixtures', 'bundle');

function connectMacosHost() {
  if (!fs.existsSync(HOST_BIN)) {
    throw new Error(`macOS host not built: ${HOST_BIN} — run \`swift build\` in hosts/macos (or \`make conformance\`)`);
  }

  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'engawa-conf-'));
  // The inline <script> must be dead under the default CSP (§7.3); introspect reports it.
  fs.writeFileSync(path.join(root, 'index.html'),
    '<!doctype html><meta charset="utf-8"><title>engawa-conformance</title>' +
    '<script>window.__inlineRan = true;</script>');
  const dataRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'engawa-data-'));

  const child = spawn(HOST_BIN, [], {
    env: {
      ...process.env,
      ENGAWA_CONFORMANCE: '1',
      ENGAWA_SHELL_JS: SHELL_JS,
      ENGAWA_APP_ROOT: root,
      ENGAWA_DATA_ROOT: dataRoot,
      ENGAWA_BUNDLE_ROOT: BUNDLE_ROOT,
    },
    stdio: ['pipe', 'pipe', 'inherit'],
  });

  let nextReqId = 1;
  const pending = new Map();
  const eventHandlers = new Map();   // topic -> Set<handler>
  let markReady;
  const ready = new Promise((res) => { markReady = res; });

  let buf = '';
  child.stdout.on('data', (chunk) => {
    buf += chunk.toString('utf8');
    let idx;
    while ((idx = buf.indexOf('\n')) >= 0) {
      const line = buf.slice(0, idx);
      buf = buf.slice(idx + 1);
      if (!line.trim()) continue;
      let msg;
      try { msg = JSON.parse(line); } catch { continue; }
      if (msg.ctl === 'ready') { markReady(); continue; }
      if (msg.ctl === 'event') {
        const set = eventHandlers.get(msg.topic);
        if (set) set.forEach((h) => { try { h(msg.payload); } catch { /* handler error is the test's */ } });
        continue;
      }
      if (msg.ctl === 'result') {
        const p = pending.get(msg.reqId);
        if (!p) continue;
        pending.delete(msg.reqId);
        if (msg.ok) p.resolve(msg.value);
        else {
          const e = new Error((msg.err && msg.err.message) || 'command failed');
          e.code = msg.err && msg.err.code;
          p.reject(e);
        }
      }
    }
  });

  child.on('exit', (code) => {
    const err = new Error(`macOS host exited (code ${code})`);
    for (const p of pending.values()) p.reject(err);
    pending.clear();
  });

  function send(obj) {
    try { child.stdin.write(JSON.stringify(obj) + '\n'); } catch { /* host gone */ }
  }

  function request(ctl) {
    const reqId = nextReqId++;
    return new Promise((resolve, reject) => {
      pending.set(reqId, { resolve, reject });
      send({ ...ctl, reqId });
    });
  }

  // Relay an event subscription to the live in-page runtime; the host posts back
  // { ctl:'event', topic, payload } which the stdout loop dispatches here.
  function on(topic, handler) {
    let set = eventHandlers.get(topic);
    if (!set) {
      set = new Set();
      eventHandlers.set(topic, set);
      send({ ctl: 'subscribe', topic });
    }
    set.add(handler);
    return function off() { set.delete(handler); };
  }

  const behavior = {
    invoke: (cmd, args) => request({ ctl: 'invoke', cmd, args: args === undefined ? null : args }),
    on,
    off: (topic, handler) => { const set = eventHandlers.get(topic); if (set) set.delete(handler); },
    // §5a PUT-body probe — not part of the shared suite; used by the spike check.
    spike: () => request({ ctl: 'spike' }),
  };

  function makeClose() {
    return () => new Promise((resolve) => {
      child.on('exit', () => {
        try { fs.rmSync(root, { recursive: true, force: true }); } catch { /* ignore */ }
        try { fs.rmSync(dataRoot, { recursive: true, force: true }); } catch { /* ignore */ }
        resolve();
      });
      send({ ctl: 'quit' });
      setTimeout(() => { try { child.kill('SIGKILL'); } catch { /* ignore */ } }, 3000);
    });
  }

  // Mirror the live in-page runtime's read-only surface (§1.1) onto the proxy, so property
  // checks (capabilities, platform, frozen) test the real engawa rather than the proxy shell.
  return ready
    .then(() => request({ ctl: 'introspect' }))
    .then((surface) => {
      const engawa = Object.assign({
        platform: surface.platform,
        contractVersion: surface.contractVersion,
        capabilities: surface.capabilities,
        inlineScriptBlocked: surface.inlineScriptBlocked,
      }, behavior);
      return {
        name: 'macos',
        engawa: surface.frozen ? Object.freeze(engawa) : engawa,
        close: makeClose(),
      };
    });
}

module.exports = { connectMacosHost };
