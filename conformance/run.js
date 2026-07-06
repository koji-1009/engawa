#!/usr/bin/env node
'use strict';
// Engawa conformance runner — entry point for `make conformance` (contract §11).
//
// Runs the suite against every configured host. A host is conformant only when all
// assertions pass; overall exit is 0 only when EVERY required host is conformant.
// The macOS reference host and the Node mock host are both required, so this stays
// red until the macOS host lands (bootstrap stage 2 part 2).

const suite = require('./suite');           // array of { name, fn }
const { createMockHost } = require('./hosts/mock/host');

// The mock host runs everywhere; the native reference host is the current platform's. On macOS this
// is byte-for-byte the old behavior (mock + macos required); on Windows it is mock + windows. A
// platform with no reference host yet (e.g. linux pre-freeze) runs mock only.
const NATIVE = {
  darwin: () => require('./hosts/macos/driver').connectMacosHost(),
  win32: () => require('./hosts/windows/driver').connectWindowsHost(),
  linux: () => require('./hosts/linux/driver').connectLinuxHost(),
};

const HOSTS = [
  { name: 'mock', required: true, connect: async () => createMockHost() },
];
if (NATIVE[process.platform]) {
  const name = process.platform === 'win32' ? 'windows' : process.platform === 'darwin' ? 'macos' : process.platform;
  HOSTS.push({ name, required: true, connect: async () => NATIVE[process.platform]() });
}

async function runHost(host) {
  let handle;
  try {
    handle = await host.connect();
  } catch (e) {
    return { name: host.name, required: host.required, available: false, error: e.message, results: [] };
  }
  const results = [];
  for (const t of suite) {
    try {
      await t.fn(handle.engawa);
      results.push({ name: t.name, ok: true });
    } catch (e) {
      results.push({ name: t.name, ok: false, error: e.message });
    }
  }
  if (handle.close) { try { await handle.close(); } catch { /* best-effort */ } }
  return { name: host.name, required: host.required, available: true, results };
}

async function main() {
  let ok = true;
  let asserted = 0;

  for (const host of HOSTS) {
    const run = await runHost(host);
    console.log(`\n# host: ${run.name}`);
    if (!run.available) {
      console.log(`  UNAVAILABLE: ${run.error}`);
      if (run.required) ok = false;
      continue;
    }
    for (const r of run.results) {
      asserted++;
      if (r.ok) {
        console.log(`  ok    ${r.name}`);
      } else {
        ok = false;
        console.log(`  FAIL  ${r.name}: ${r.error}`);
      }
    }
    if (run.results.length === 0) {
      console.log('  (no assertions)');
    }
  }

  console.log(`\n${ok ? 'PASS' : 'FAIL'} — ${asserted} assertions across ${HOSTS.length} host(s)`);
  process.exit(ok ? 0 : 1);
}

main().catch((e) => { console.error(e); process.exit(1); });
