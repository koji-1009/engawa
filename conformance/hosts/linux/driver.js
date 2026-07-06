'use strict';
// Linux reference-host driver for the conformance runner — the WebKitGTK twin of hosts/macos/driver.js
// and hosts/windows/driver.js.
//
// Launches the C++ host (hosts/linux/build/EngawaHost) in conformance mode and exposes an
// `engawa`-shaped proxy. Each invoke() is forwarded over a stdio control channel to the host, which
// runs it through the REAL in-page shell.js (round-trip to native dispatch) and reports back — so the
// same suite that runs against the mock and other native hosts here exercises the full live stack.
// The control-channel protocol is identical to the other drivers; only the binary and the engine
// floor straddle differ (WebKitGTK 2.x vs WebView2/Chromium — see ENGINE_FLOOR_SAMPLES).

const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');
const os = require('os');
const crypto = require('crypto');
const { StringDecoder } = require('string_decoder');

const REPO = path.join(__dirname, '..', '..', '..');
const HOST_BIN = resolveHostBin();
const SHELL_JS = path.join(REPO, 'shell-js', 'shell.js');
const BUNDLE_ROOT = path.join(REPO, 'conformance', 'fixtures', 'bundle');

// The WebKitGTK engine floor is 2.28.0 (spec/contract.md §9, hosts/linux EngineFloor). Straddle it:
// 2.20 is below, 99999 above. (A fixed '600.0' would be below WebKit's floor but ABOVE any Chromium
// floor, which is why the suite takes these from the driver — engine.test.js.)
const ENGINE_FLOOR_SAMPLES = { below: '2.20.0', above: '99999' };

function resolveHostBin() {
  // ENGAWA_HOST_BIN lets the suite run against a specific build; otherwise the CMake output produced
  // by `make host-linux` (hosts/linux/build.sh).
  if (process.env.ENGAWA_HOST_BIN) return process.env.ENGAWA_HOST_BIN;
  return path.join(REPO, 'hosts', 'linux', 'build', 'EngawaHost');
}

// §9: spawn a throwaway host with a faked engine version and observe whether it rejects the floor
// (no boot) or reaches ready. Independent of the connected host.
function checkEngineFloor(fakeVersion) {
  return spawnProbe({ ENGAWA_FAKE_ENGINE_VERSION: String(fakeVersion) }, (m, finish) => {
    if (m.ctl === 'floorRejected') finish({ rejected: true, detected: m.detected, required: m.required });
    else if (m.ctl === 'ready') finish({ rejected: false, booted: true });
  }, { rejected: false, booted: false, timeout: true });
}

// §10: spawn a host that wipes WebView storage at boot and confirm it still reaches ready.
function checkStorageWipe() {
  return spawnProbe({ ENGAWA_WIPE_STORAGE: '1' }, (m, finish) => {
    if (m.ctl === 'ready') finish({ booted: true });
  }, { booted: false });
}

// §6/§7: spawn a host that starts on about:blank and report its injection state (dead __shell, no
// engawa). A dedicated host, so the main suite host's control channel is left on app://.
function checkNonAppInjection() {
  return spawnProbe({ ENGAWA_START_URL: 'about:blank' }, (m, finish, child) => {
    if (m.ctl === 'ready') { try { child.stdin.write(JSON.stringify({ ctl: 'nonAppCheck', reqId: 1 }) + '\n'); } catch { /* gone */ } }
    else if (m.ctl === 'result') finish(m.value);
  }, { timeout: true });
}

// Shared throwaway-host driver for the §9/§10/§6 probes.
function spawnProbe(extraEnv, onMessage, timeoutValue) {
  return new Promise((resolve) => {
    const aroot = fs.mkdtempSync(path.join(os.tmpdir(), 'engawa-probe-'));
    fs.writeFileSync(path.join(aroot, 'index.html'), '<!doctype html><meta charset="utf-8"><title>x</title>');
    const droot = fs.mkdtempSync(path.join(os.tmpdir(), 'engawa-probedata-'));
    const c = spawn(HOST_BIN, [], {
      env: { ...process.env, ENGAWA_CONFORMANCE: '1', ENGAWA_SHELL_JS: SHELL_JS,
             ENGAWA_APP_ROOT: aroot, ENGAWA_DATA_ROOT: droot, ...extraEnv },
      stdio: ['pipe', 'pipe', 'ignore'],
    });
    let done = false, b = '';
    const finish = (r) => {
      if (done) return; done = true;
      try { c.kill("SIGKILL"); } catch { /* ignore */ }
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
        onMessage(m, finish, c);
      }
    });
    setTimeout(() => finish(timeoutValue), 15000);
  });
}

function connectLinuxHost() {
  if (!fs.existsSync(HOST_BIN)) {
    throw new Error(`Linux host not built: ${HOST_BIN} — run \`make host-linux\` (hosts/linux/build.sh)`);
  }

  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'engawa-conf-'));
  // §7.3: the inline <script> must be dead, but the EXTERNAL app: script must load (script-src app:).
  fs.writeFileSync(path.join(root, 'probe.js'), 'window.__externalRan = true;');
  // §6: an app:// iframe must NOT receive __shell (top-level app:// only). frame.js runs in the iframe
  // (proving CSP allows app: scripts in subframes too) and reports to the parent.
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

  // Dev trust root (contract §7.1): an ephemeral ed25519 keypair; the host takes the public key via
  // the environment (conformance), the suite signs payloads with the private key. Raw 32-byte pubkey → base64.
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
  const eventHandlers = new Map();
  let markReady;
  const ready = new Promise((res) => { markReady = res; });

  // StringDecoder buffers a multibyte UTF-8 sequence split across stdout chunks (surfaced by the
  // 1 MiB unicode test), which a naive chunk.toString('utf8') would corrupt at the boundary.
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
    const err = new Error(`Linux host exited (code ${code})`);
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
    ioPut: (url, b) => request({ ctl: 'ioPut', url, dataB64: Buffer.from(b).toString('base64') }),
    ioGet: (url) => request({ ctl: 'ioGet', url }).then((v) => Buffer.from(v.base64, 'base64')),
    frameCheck: () => request({ ctl: 'frameCheck' }),
    simulateRenderCrash: () => request({ ctl: 'simulateRenderCrash' }),
    checkNonAppInjection: () => checkNonAppInjection(),
    checkEngineFloor: (v) => checkEngineFloor(v),
    checkStorageWipe: () => checkStorageWipe(),
    engineFloorSamples: ENGINE_FLOOR_SAMPLES,
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
      setTimeout(() => { try { child.kill("SIGKILL"); } catch { /* ignore */ } }, 3000);
    });
  }

  return ready
    .then(() => request({ ctl: 'introspect' }))
    .then((surface) => {
      const engawa = Object.assign({
        platform: surface.platform,
        contractVersion: surface.contractVersion,
        capabilities: surface.capabilities,
        inlineScriptBlocked: surface.inlineScriptBlocked,
        externalScriptRan: surface.externalScriptRan,
      }, behavior);
      return {
        name: 'linux',
        engawa: surface.frozen ? Object.freeze(engawa) : engawa,
        close: makeClose(),
      };
    });
}

module.exports = { connectLinuxHost };
