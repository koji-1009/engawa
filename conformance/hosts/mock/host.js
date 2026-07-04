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

const SHELL_JS = fs.readFileSync(
  path.join(__dirname, '..', '..', '..', 'shell-js', 'shell.js'), 'utf8');

// Command handlers served by the mock host, keyed by full `namespace.command`.
// `echo` remains as the slice fixture; §4 namespaces are added here as they land.
function defaultHandlers() {
  const dataRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'engawa-mock-'));
  const ensure = (p) => { fs.mkdirSync(p, { recursive: true }); return p; };
  return {
    echo: async (args) => args,

    // path (spec/commands/path.md)
    'path.appData': async () => ensure(path.join(dataRoot, 'data')),
    'path.appConfig': async () => ensure(path.join(dataRoot, 'config')),
    'path.appCache': async () => ensure(path.join(dataRoot, 'cache')),
    'path.home': async () => os.homedir(),
    'path.temp': async () => os.tmpdir(),
  };
}

// Build one runtime instance backed by in-process handlers.
function createMockHost(options = {}) {
  const handlers = Object.assign(defaultHandlers(), options.handlers || {});
  const capabilities = options.capabilities ||
    [...new Set(Object.keys(handlers).map((k) => k.split('.')[0]))];

  let outbound = [];         // frames queued for delivery
  let scheduled = false;

  const sandbox = { console };
  sandbox.globalThis = sandbox;
  const context = vm.createContext(sandbox);

  const shell = {
    contractVersion: '1.0',
    platform: 'mock',
    capabilities,
    postMessage(jsonString) { onRequest(jsonString); },
  };
  sandbox.__shell = shell;

  vm.runInContext(SHELL_JS, context, { filename: 'shell.js' });

  // Flow control (contract §2.1): drain the outbound queue into at most one
  // `_deliver` eval per tick, frames batched into a single JSON array.
  function scheduleFlush() {
    if (scheduled) return;
    scheduled = true;
    queueMicrotask(() => {
      scheduled = false;
      if (outbound.length === 0) return;
      const batch = outbound;
      outbound = [];
      shell._deliver(JSON.stringify(batch));
    });
  }

  function emit(frame) { outbound.push(frame); scheduleFlush(); }

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
    emitEvent(topic, payload) { emit({ t: 'evt', topic, payload }); },
  };
}

module.exports = { createMockHost };
