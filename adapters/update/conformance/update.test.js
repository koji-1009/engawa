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
// An app-update payload is a tarball of the new asset tree (§8: unpacks to a fresh directory).
function writePayload(html) {
  var src = fs.mkdtempSync(path.join(os.tmpdir(), 'engawa-updsrc-'));
  fs.writeFileSync(path.join(src, 'index.html'), html);
  var tar = path.join(os.tmpdir(), 'engawa-upd-' + Date.now() + '-' + (seq++) + '.tar');
  child_process.execFileSync('tar', ['-cf', tar, '-C', src, '.']);
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

test('update.evaluate applies the §8 compatibility rule (app-update vs full-update)', async function (engawa) {
  if (!has(engawa)) return;
  var appOnly = await engawa.invoke('update.evaluate', {
    manifest: { app: { version: '2.0.0', contractRequired: '1.0', capabilitiesRequired: ['sqlite'] } },
    provided: { contractProvided: '1.0', capabilities: ['sqlite', 'update'] },
  });
  assertEqual(appOnly.mode, 'app-update', 'satisfied base → app-update');

  var events = [];
  var off = engawa.on('update.readyToInstall', function (p) { events.push(p); });
  var full = await engawa.invoke('update.evaluate', {
    manifest: { app: { version: '2.0.0', contractRequired: '1.0', capabilitiesRequired: ['not-served'] } },
    provided: { contractProvided: '1.0', capabilities: ['sqlite', 'update'] },
  });
  assertEqual(full.mode, 'full-update', 'missing capability → full-update');
  await waitFor(function () { return events.length > 0; }, 2000);
  off();
  assertEqual(events[0].version, '2.0.0', 'readyToInstall carried the version');
});

test('update.install acknowledges the OS handoff', async function (engawa) {
  if (!has(engawa)) return;
  assertEqual((await engawa.invoke('update.install')).handoff, true, 'handoff');
});
