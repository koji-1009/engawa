'use strict';
// Conformance for the update adapter (adapters/update/spec.md). Update is deeply host-coupled
// (A/B slots, relaunch, ed25519 verification), so it runs only where the host serves it — the
// macOS host — and skips on the rendererless mock host. Relaunches are driven via the
// update.__relaunch testability hook; payloads are signed with the driver's dev key (signFile).

var h = require('../../../conformance/harness');
var fs = require('fs');
var os = require('os');
var path = require('path');
var child_process = require('child_process');
var test = h.test, assert = h.assert, assertEqual = h.assertEqual;

function has(engawa) { return engawa.capabilities.indexOf('update') >= 0; }
function other(slot) { return slot === 'a' ? 'b' : 'a'; }
function delay(ms) { return new Promise(function (r) { setTimeout(r, ms); }); }
async function waitFor(cond, timeoutMs) {
  var start = Date.now();
  while (!cond()) { if (Date.now() - start > timeoutMs) throw new Error('timed out'); await delay(10); }
}

var seq = 0;
// Use the Windows system bsdtar on win32: it handles drive-letter (C:\…) archive paths, whereas the
// GNU tar that ships with Git Bash misreads "C:" as a remote host ("Cannot connect to C"). Elsewhere,
// plain `tar` from PATH.
var TAR_BIN = process.platform === 'win32'
  ? path.join(process.env.SystemRoot || 'C:\\Windows', 'System32', 'tar.exe')
  : 'tar';

// An app-update payload is a tarball of the new asset tree (§8: unpacks to a fresh directory).
function writePayload(html) {
  var src = fs.mkdtempSync(path.join(os.tmpdir(), 'engawa-updsrc-'));
  fs.writeFileSync(path.join(src, 'index.html'), html);
  var tar = path.join(os.tmpdir(), 'engawa-upd-' + Date.now() + '-' + (seq++) + '.tar');
  child_process.execFileSync(TAR_BIN, ['-cf', tar, '-C', src, '.']);
  return tar;
}

test('update: sign → stage → relaunch → confirmBoot adopts the pending slot', async function (engawa) {
  if (!has(engawa)) return;
  var st0 = await engawa.invoke('update.status');
  var target = other(st0.currentSlot);

  var payload = writePayload('<!doctype html><meta charset="utf-8"><title>v1.1</title>');
  var signed = engawa.signFile(payload);
  var r = await engawa.invoke('update.stageAppUpdate', { payloadPath: payload, hash: signed.hash, signature: signed.signature, version: '1.1.0' });
  assertEqual(r.staged, true, 'staged');
  assertEqual((await engawa.invoke('update.status')).hasPending, true, 'pending reserved');

  var rl = await engawa.invoke('update.__relaunch');
  assertEqual(rl.bootingSlot, target, 'boots the pending slot');
  assertEqual(rl.currentSlot, st0.currentSlot, 'current not adopted before confirmBoot');

  await engawa.invoke('update.confirmBoot');
  var st2 = await engawa.invoke('update.status');
  assertEqual(st2.currentSlot, target, 'pending adopted after confirmBoot');
  assertEqual(st2.hasPending, false, 'no pending after adoption');
  assertEqual(st2.version, '1.1.0', 'version advanced');
  fs.rmSync(payload, { force: true });
});

test('update: staging again while a pending slot is booted never targets the live slot', async function (engawa) {
  if (!has(engawa)) return;
  var st0 = await engawa.invoke('update.status');
  var pending = other(st0.currentSlot);

  var p1 = writePayload('<title>first</title>');
  var s1 = engawa.signFile(p1);
  await engawa.invoke('update.stageAppUpdate', { payloadPath: p1, hash: s1.hash, signature: s1.signature, version: '1.1.0' });
  assertEqual((await engawa.invoke('update.status')).pendingSlot, pending, 'first stage targets the non-live slot');

  // Boot the pending slot but do NOT confirm — now bootingSlot != currentSlot.
  var rl = await engawa.invoke('update.__relaunch');
  assertEqual(rl.bootingSlot, pending, 'booted the pending slot, unconfirmed');

  // Stage a second update while the pending slot is the live one. It must target the OTHER slot
  // (the old current), never the slot app:// is currently serving — else the live root is wiped.
  var p2 = writePayload('<title>second</title>');
  var s2 = engawa.signFile(p2);
  await engawa.invoke('update.stageAppUpdate', { payloadPath: p2, hash: s2.hash, signature: s2.signature, version: '1.2.0' });
  var st2 = await engawa.invoke('update.status');
  assert(st2.pendingSlot !== rl.bootingSlot, 'second stage did not target the live (booted) slot');
  assertEqual(st2.pendingSlot, st0.currentSlot, 'second stage targeted the non-live slot');

  // Clean up: roll the unconfirmed pending back (auto-rollback) so later tests start fresh.
  for (var i = 0; i < 5 && (await engawa.invoke('update.status')).hasPending; i++) {
    await engawa.invoke('update.__relaunch');
  }
  assertEqual((await engawa.invoke('update.status')).hasPending, false, 'pending cleared for later tests');
  fs.rmSync(p1, { force: true }); fs.rmSync(p2, { force: true });
});

test('update: bad signature → ESIGNATURE, no slot change', async function (engawa) {
  if (!has(engawa)) return;
  var st0 = await engawa.invoke('update.status');
  var payload = writePayload('<title>tampered</title>');
  var signed = engawa.signFile(payload);
  var badSig = 'AAAA' + signed.signature.slice(4);
  var err = null;
  try {
    await engawa.invoke('update.stageAppUpdate', { payloadPath: payload, hash: signed.hash, signature: badSig, version: '9.9.9' });
  } catch (e) { err = e; }
  assert(err, 'expected rejection');
  assertEqual(err.code, 'ESIGNATURE', 'code');
  var st1 = await engawa.invoke('update.status');
  assertEqual(st1.hasPending, false, 'nothing staged');
  assertEqual(st1.currentSlot, st0.currentSlot, 'current unchanged');
  fs.rmSync(payload, { force: true });
});

test('update: content-hash mismatch → EHASH', async function (engawa) {
  if (!has(engawa)) return;
  var payload = writePayload('<title>v3</title>');
  var signed = engawa.signFile(payload);
  var wrong = signed.hash.slice(0, -1) + (signed.hash.slice(-1) === '0' ? '1' : '0');
  var err = null;
  try {
    await engawa.invoke('update.stageAppUpdate', { payloadPath: payload, hash: wrong, signature: signed.signature, version: '3.0.0' });
  } catch (e) { err = e; }
  assert(err, 'expected rejection');
  assertEqual(err.code, 'EHASH', 'code');
  fs.rmSync(payload, { force: true });
});

test('update: a verified-but-broken payload rolls back after 2 unconfirmed launches', async function (engawa) {
  if (!has(engawa)) return;
  var stay = (await engawa.invoke('update.status')).currentSlot;
  var target = other(stay);
  var payload = writePayload('<title>broken</title>');
  var signed = engawa.signFile(payload);
  await engawa.invoke('update.stageAppUpdate', { payloadPath: payload, hash: signed.hash, signature: signed.signature, version: '2.0.0' });

  assertEqual((await engawa.invoke('update.__relaunch')).bootingSlot, target, 'attempt 1 boots pending');
  assertEqual((await engawa.invoke('update.__relaunch')).bootingSlot, target, 'attempt 2 boots pending');
  var r3 = await engawa.invoke('update.__relaunch');
  assertEqual(r3.bootingSlot, stay, 'attempt 3 rolls back to the previous slot');
  assertEqual(r3.hasPending, false, 'pending discarded');
  assertEqual((await engawa.invoke('update.status')).currentSlot, stay, 'current is the previous slot');
  fs.rmSync(payload, { force: true });
});

test('update: staging a fresh payload resets the rollback budget (§8)', async function (engawa) {
  if (!has(engawa)) return;
  var start = (await engawa.invoke('update.status')).currentSlot;

  // Stage payload A and consume ONE of its two launch attempts (one unconfirmed boot).
  var pa = writePayload('<title>A</title>');
  var sa = engawa.signFile(pa);
  await engawa.invoke('update.stageAppUpdate', { payloadPath: pa, hash: sa.hash, signature: sa.signature, version: '1.1.0' });
  await engawa.invoke('update.__relaunch');   // A: attempt 1 consumed (health.attempts = 1)

  // Stage payload B on top of A's partially-spent budget. B MUST get a full, fresh 2 attempts —
  // it must not inherit A's already-consumed attempt (that would be a diminished budget).
  var pb = writePayload('<title>B</title>');
  var sb = engawa.signFile(pb);
  await engawa.invoke('update.stageAppUpdate', { payloadPath: pb, hash: sb.hash, signature: sb.signature, version: '1.2.0' });

  // Two unconfirmed launches must NOT yet roll B back — it still has its full 2 fresh attempts.
  // Without the budget reset, B would inherit attempts=1 and roll back on the 2nd launch here.
  assertEqual((await engawa.invoke('update.__relaunch')).hasPending, true, 'B still pending after fresh attempt 1');
  assertEqual((await engawa.invoke('update.__relaunch')).hasPending, true, 'B still pending after fresh attempt 2');
  // The third unconfirmed launch exhausts B's fresh budget and rolls back.
  var r3 = await engawa.invoke('update.__relaunch');
  assertEqual(r3.hasPending, false, 'B rolled back only after its full 2 fresh attempts');
  assertEqual((await engawa.invoke('update.status')).currentSlot, start, 'rolled back to the original slot');
  fs.rmSync(pa, { force: true }); fs.rmSync(pb, { force: true });
});

test('update.evaluate classifies mode only — it does NOT announce installable (§153)', async function (engawa) {
  if (!has(engawa)) return;
  var appOnly = await engawa.invoke('update.evaluate', {
    manifest: { app: { version: '2.0.0', contractRequired: '0.1.0', capabilitiesRequired: ['sqlite'] } },
    provided: { contractProvided: '0.1.0', capabilities: ['sqlite', 'update'] },
  });
  assertEqual(appOnly.mode, 'app-update', 'satisfied base → app-update');

  // full-update mode is classified, but NO readyToInstall is emitted: nothing is verified yet (§153).
  var events = [];
  var off = engawa.on('update.readyToInstall', function (p) { events.push(p); });
  var full = await engawa.invoke('update.evaluate', {
    manifest: { app: { version: '2.0.0', contractRequired: '0.1.0', capabilitiesRequired: ['not-served'] } },
    provided: { contractProvided: '0.1.0', capabilities: ['sqlite', 'update'] },
  });
  assertEqual(full.mode, 'full-update', 'missing capability → full-update');
  await delay(300);   // give any (erroneous) event time to arrive
  off();
  assertEqual(events.length, 0, 'evaluate must NOT emit readyToInstall — nothing is verified yet');
});

test('update.stageBaseUpdate verifies the installer before announcing it (§153)', async function (engawa) {
  if (!has(engawa)) return;

  // A verified base installer → readyToInstall carries the version; staged.
  var payload = writePayload('<title>base v2</title>');
  var signed = engawa.signFile(payload);
  var events = [];
  var off = engawa.on('update.readyToInstall', function (p) { events.push(p); });
  var r = await engawa.invoke('update.stageBaseUpdate', { payloadPath: payload, hash: signed.hash, signature: signed.signature, version: '2.0.0' });
  assertEqual(r.staged, true, 'staged');
  await waitFor(function () { return events.length > 0; }, 2000);
  assertEqual(events[events.length - 1].version, '2.0.0', 'readyToInstall carried the version after verification');

  // A tampered signature → ESIGNATURE, and NO readyToInstall (never announced unverified).
  var before = events.length;
  var badSig = 'AAAA' + signed.signature.slice(4);
  var err = null;
  try {
    await engawa.invoke('update.stageBaseUpdate', { payloadPath: payload, hash: signed.hash, signature: badSig, version: '9.9.9' });
  } catch (e) { err = e; }
  assertEqual(err && err.code, 'ESIGNATURE', 'a bad base-installer signature rejects with ESIGNATURE');
  await delay(300);
  off();
  assertEqual(events.length, before, 'no readyToInstall for an unverified base installer');
  fs.rmSync(payload, { force: true });
});

test('update.install acknowledges the OS handoff', async function (engawa) {
  if (!has(engawa)) return;
  assertEqual((await engawa.invoke('update.install')).handoff, true, 'handoff');
});
