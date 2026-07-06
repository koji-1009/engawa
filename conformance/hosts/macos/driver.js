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
const crypto = require('crypto');
const { StringDecoder } = require('string_decoder');

const REPO = path.join(__dirname, '..', '..', '..');
const HOST_BIN = path.join(REPO, 'hosts', 'macos', '.build', 'debug', 'EngawaHost');
const SHELL_JS = path.join(REPO, 'shell-js', 'shell.js');
const BUNDLE_ROOT = path.join(REPO, 'conformance', 'fixtures', 'bundle');

// The WebKit engine floor is 605.1.15 (spec/contract.md §9, EngawaHost.engineFloor). Straddle it:
// 600.0 is below, 99999 above. The suite draws these from the driver (engine.test.js) because the
// floor's version scheme is per-engine — a fixed pair would misjudge the Chromium/WebKitGTK hosts.
const ENGINE_FLOOR_SAMPLES = { below: '600.0', above: '99999' };

// §9: spawn a throwaway host with a faked engine version and observe whether it rejects the
// engine floor (no boot) or reaches ready. Independent of the connected host.
function checkEngineFloor(fakeVersion) {
  return new Promise((resolve) => {
    const aroot = fs.mkdtempSync(path.join(os.tmpdir(), 'engawa-floor-'));
    fs.writeFileSync(path.join(aroot, 'index.html'), '<!doctype html><meta charset="utf-8"><title>x</title>');
    const droot = fs.mkdtempSync(path.join(os.tmpdir(), 'engawa-floordata-'));
    const c = spawn(HOST_BIN, [], {
      env: { ...process.env, ENGAWA_CONFORMANCE: '1', ENGAWA_SHELL_JS: SHELL_JS,
             ENGAWA_APP_ROOT: aroot, ENGAWA_DATA_ROOT: droot, ENGAWA_FAKE_ENGINE_VERSION: String(fakeVersion) },
      stdio: ['ignore', 'pipe', 'ignore'],
    });
    let done = false, b = '';
    const finish = (r) => {
      if (done) return; done = true;
      try { c.kill('SIGKILL'); } catch { /* ignore */ }
      try { fs.rmSync(aroot, { recursive: true, force: true }); } catch { /* ignore */ }
      try { fs.rmSync(droot, { recursive: true, force: true }); } catch { /* ignore */ }
      resolve(r);
    };
    c.stdout.on('data', (d) => {
      b += d.toString('utf8');
      let i;
      while ((i = b.indexOf('\n')) >= 0) {
        const line = b.slice(0, i); b = b.slice(i + 1);
        if (!line.trim()) continue;
        let m; try { m = JSON.parse(line); } catch { continue; }
        if (m.ctl === 'floorRejected') finish({ rejected: true, detected: m.detected, required: m.required });
        else if (m.ctl === 'ready') finish({ rejected: false, booted: true });
      }
    });
    // A hang (no floorRejected, no ready) is a FAILURE, not a pass — booted stays false so the
    // above-floor assertion (booted === true) can tell a real boot from a host that never started.
    setTimeout(() => finish({ rejected: false, booted: false, timeout: true }), 8000);
  });
}

// §9: spawn a host with engine detection FORCED to fail and observe that it fails closed —
// rejects the floor (no boot) and reports a detected version — rather than assuming a version
// above the floor. Exercises the detection-failure fallback, which ENGAWA_FAKE_ENGINE_VERSION
// (a successful substitution) cannot reach.
function checkEngineUndetected() {
  return new Promise((resolve) => {
    const aroot = fs.mkdtempSync(path.join(os.tmpdir(), 'engawa-undet-'));
    fs.writeFileSync(path.join(aroot, 'index.html'), '<!doctype html><meta charset="utf-8"><title>x</title>');
    const droot = fs.mkdtempSync(path.join(os.tmpdir(), 'engawa-undetdata-'));
    const c = spawn(HOST_BIN, [], {
      env: { ...process.env, ENGAWA_CONFORMANCE: '1', ENGAWA_SHELL_JS: SHELL_JS,
             ENGAWA_APP_ROOT: aroot, ENGAWA_DATA_ROOT: droot, ENGAWA_FORCE_ENGINE_UNDETECTED: '1' },
      stdio: ['ignore', 'pipe', 'ignore'],
    });
    let done = false, b = '';
    const finish = (r) => {
      if (done) return; done = true;
      try { c.kill('SIGKILL'); } catch { /* ignore */ }
      try { fs.rmSync(aroot, { recursive: true, force: true }); } catch { /* ignore */ }
      try { fs.rmSync(droot, { recursive: true, force: true }); } catch { /* ignore */ }
      resolve(r);
    };
    c.stdout.on('data', (d) => {
      b += d.toString('utf8');
      let i;
      while ((i = b.indexOf('\n')) >= 0) {
        const line = b.slice(0, i); b = b.slice(i + 1);
        if (!line.trim()) continue;
        let m; try { m = JSON.parse(line); } catch { continue; }
        if (m.ctl === 'floorRejected') finish({ rejected: true, detected: m.detected, required: m.required });
        else if (m.ctl === 'ready') finish({ rejected: false, booted: true });
      }
    });
    setTimeout(() => finish({ rejected: false, booted: false, timeout: true }), 8000);
  });
}

// §10: spawn a host that wipes WebView storage at boot and confirm it still reaches ready.
function checkStorageWipe() {
  return new Promise((resolve) => {
    const aroot = fs.mkdtempSync(path.join(os.tmpdir(), 'engawa-wipe-'));
    fs.writeFileSync(path.join(aroot, 'index.html'), '<!doctype html><meta charset="utf-8"><title>x</title>');
    const droot = fs.mkdtempSync(path.join(os.tmpdir(), 'engawa-wipedata-'));
    const c = spawn(HOST_BIN, [], {
      env: { ...process.env, ENGAWA_CONFORMANCE: '1', ENGAWA_SHELL_JS: SHELL_JS,
             ENGAWA_APP_ROOT: aroot, ENGAWA_DATA_ROOT: droot, ENGAWA_WIPE_STORAGE: '1' },
      stdio: ['ignore', 'pipe', 'ignore'],
    });
    let done = false, b = '';
    const finish = (r) => {
      if (done) return; done = true;
      try { c.kill('SIGKILL'); } catch { /* ignore */ }
      try { fs.rmSync(aroot, { recursive: true, force: true }); } catch { /* ignore */ }
      try { fs.rmSync(droot, { recursive: true, force: true }); } catch { /* ignore */ }
      resolve(r);
    };
    c.stdout.on('data', (d) => {
      b += d.toString('utf8');
      let i;
      while ((i = b.indexOf('\n')) >= 0) {
        const line = b.slice(0, i); b = b.slice(i + 1);
        if (!line.trim()) continue;
        let m; try { m = JSON.parse(line); } catch { continue; }
        if (m.ctl === 'ready') finish({ booted: true });
      }
    });
    setTimeout(() => finish({ booted: false }), 8000);
  });
}

// §6/§7: spawn a host that starts on about:blank and report its injection state (dead __shell,
// no engawa). Navigating the main suite host away from app:// would break its control channel,
// so use a dedicated host.
function checkNonAppInjection() {
  return new Promise((resolve) => {
    const aroot = fs.mkdtempSync(path.join(os.tmpdir(), 'engawa-nonapp-'));
    fs.writeFileSync(path.join(aroot, 'index.html'), '<!doctype html><meta charset="utf-8"><title>x</title>');
    const droot = fs.mkdtempSync(path.join(os.tmpdir(), 'engawa-nonappdata-'));
    const c = spawn(HOST_BIN, [], {
      env: { ...process.env, ENGAWA_CONFORMANCE: '1', ENGAWA_SHELL_JS: SHELL_JS,
             ENGAWA_APP_ROOT: aroot, ENGAWA_DATA_ROOT: droot, ENGAWA_START_URL: 'about:blank' },
      stdio: ['pipe', 'pipe', 'ignore'],
    });
    let done = false, b = '';
    const finish = (r) => {
      if (done) return; done = true;
      try { c.kill('SIGKILL'); } catch { /* ignore */ }
      try { fs.rmSync(aroot, { recursive: true, force: true }); } catch { /* ignore */ }
      try { fs.rmSync(droot, { recursive: true, force: true }); } catch { /* ignore */ }
      resolve(r);
    };
    c.stdout.on('data', (d) => {
      b += d.toString('utf8');
      let i;
      while ((i = b.indexOf('\n')) >= 0) {
        const line = b.slice(0, i); b = b.slice(i + 1);
        if (!line.trim()) continue;
        let m; try { m = JSON.parse(line); } catch { continue; }
        if (m.ctl === 'ready') { try { c.stdin.write(JSON.stringify({ ctl: 'nonAppCheck', reqId: 1 }) + '\n'); } catch { /* gone */ } }
        else if (m.ctl === 'result') finish(m.value);
      }
    });
    setTimeout(() => finish({ timeout: true }), 8000);
  });
}

function connectMacosHost() {
  if (!fs.existsSync(HOST_BIN)) {
    throw new Error(`macOS host not built: ${HOST_BIN} — run \`swift build\` in hosts/macos (or \`make conformance\`)`);
  }

  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'engawa-conf-'));
  // §7.3: the inline <script> must be dead, but the EXTERNAL app: script must load
  // (script-src app:). introspect reports both.
  fs.writeFileSync(path.join(root, 'probe.js'), 'window.__externalRan = true;');
  // §6: an app:// iframe must NOT receive __shell (top-level app:// only). frame.js runs in
  // the iframe (proving CSP allows app: scripts in subframes too) and reports to the parent.
  fs.writeFileSync(path.join(root, 'frame.js'),
    'window.parent.__iframeLoaded = true; window.parent.__iframeHadShell = (typeof window.__shell !== "undefined");');
  fs.writeFileSync(path.join(root, 'frame.html'),
    '<!doctype html><meta charset="utf-8"><script src="frame.js"></script>');
  fs.writeFileSync(path.join(root, 'index.html'),
    '<!doctype html><meta charset="utf-8"><title>engawa-conformance</title>' +
    '<script>window.__inlineRan = true;</script>' +
    '<script src="probe.js"></script>' +
    '<iframe src="frame.html"></iframe>');
  const dataRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'engawa-data-'));

  // Dev trust root (contract §7.1): an ephemeral ed25519 keypair; the host embeds the public
  // key, the suite signs payloads with the private key (stands in for the Makefile-generated
  // dev key). The raw 32-byte public key goes to the host as base64.
  const { publicKey, privateKey } = crypto.generateKeyPairSync('ed25519');
  const trustRootB64 = Buffer.from(publicKey.export({ format: 'jwk' }).x, 'base64url').toString('base64');

  const child = spawn(HOST_BIN, [], {
    env: {
      ...process.env,
      ENGAWA_CONFORMANCE: '1',
      ENGAWA_SHELL_JS: SHELL_JS,
      ENGAWA_APP_ROOT: root,
      ENGAWA_DATA_ROOT: dataRoot,
      ENGAWA_BUNDLE_ROOT: BUNDLE_ROOT,
      ENGAWA_TRUST_ROOT: trustRootB64,
    },
    stdio: ['pipe', 'pipe', 'inherit'],
  });

  let nextReqId = 1;
  const pending = new Map();
  const eventHandlers = new Map();   // topic -> Set<handler>
  let markReady;
  const ready = new Promise((res) => { markReady = res; });

  // StringDecoder buffers a multibyte UTF-8 sequence split across stdout chunks; a naive
  // chunk.toString('utf8') would corrupt it at the boundary (surfaced by the 1 MiB unicode test).
  let buf = '';
  const decoder = new StringDecoder('utf8');
  child.stdout.on('data', (chunk) => {
    buf += decoder.write(chunk);
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
    // §5a binary I/O: the fetch to app://io happens in-page; the runner drives it via these.
    // Data crosses the control channel base64-encoded (never the message channel).
    ioPut: (url, buf) => request({ ctl: 'ioPut', url, dataB64: Buffer.from(buf).toString('base64') }),
    ioGet: (url) => request({ ctl: 'ioGet', url }).then((v) => Buffer.from(v.base64, 'base64')),
    // §5a — the CORS headers the host actually emitted on the last app://io response. In-page JS
    // can't read Access-Control-Allow-Origin off a cross-origin fetch, so the host reports it.
    ioCorsHeaders: () => request({ ctl: 'ioCorsHeaders' }),
    // §6 injection matrix — reports whether the app:// iframe received __shell.
    frameCheck: () => request({ ctl: 'frameCheck' }),
    // §10 — run the renderer-crash recovery accounting + emit app.renderCrashed.
    simulateRenderCrash: () => request({ ctl: 'simulateRenderCrash' }),
    // §6/§7 — injection state on a non-app (about:blank) document, via a dedicated host.
    checkNonAppInjection: () => checkNonAppInjection(),
    // §9 engine floor — spawns a throwaway host with a faked engine version.
    checkEngineFloor: (v) => checkEngineFloor(v),
    // §9 fail-closed — spawns a host with engine detection forced to fail.
    checkEngineUndetected: () => checkEngineUndetected(),
    // §10 boot-after-storage-wipe — spawns a host that wipes WebView storage at boot.
    checkStorageWipe: () => checkStorageWipe(),
    // Sign a payload file with the dev private key (§7.1) — used by the update conformance.
    signFile: (p) => {
      const digest = crypto.createHash('sha256').update(fs.readFileSync(p)).digest();
      return { hash: digest.toString('hex'), signature: crypto.sign(null, digest, privateKey).toString('base64') };
    },
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
      // The app version declared in the bundle manifest (engawa.json) the host was pointed at —
      // what app.version must report (spec/commands/app.md), distinct from ENGAWA_APP_VERSION
      // (unset here) and the host binary's Info.plist. Lets the suite assert manifest precedence.
      let manifestVersion;
      try { manifestVersion = JSON.parse(fs.readFileSync(path.join(BUNDLE_ROOT, 'engawa.json'), 'utf8')).version; } catch { /* none */ }
      const engawa = Object.assign({
        platform: surface.platform,
        contractVersion: surface.contractVersion,
        capabilities: surface.capabilities,
        inlineScriptBlocked: surface.inlineScriptBlocked,
        externalScriptRan: surface.externalScriptRan,
        manifestVersion,
        engineFloorSamples: ENGINE_FLOOR_SAMPLES,
      }, behavior);
      return {
        name: 'macos',
        engawa: surface.frozen ? Object.freeze(engawa) : engawa,
        close: makeClose(),
      };
    });
}

module.exports = { connectMacosHost };
