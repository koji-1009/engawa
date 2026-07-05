'use strict';
// fs namespace (spec/commands/fs.md). Exercised host-agnostically: paths are built under
// path.appData, so the same test runs against the mock and macOS hosts. Test closures run
// in the runner (Node), so Date.now/Math.random are available for unique scratch dirs.

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert, assertEqual = h.assertEqual;

  var counter = 0;
  async function scratchDir(engawa) {
    var base = await engawa.invoke('path.appData');
    var dir = base + '/fs-test-' + Date.now() + '-' + (counter++) + '-' + Math.floor(Math.random() * 1e6);
    await engawa.invoke('fs.mkdir', { path: dir, recursive: true });
    return dir;
  }

  test('fs write → read → stat → remove round-trip (unicode intact)', async function (engawa) {
    var dir = await scratchDir(engawa);
    var file = dir + '/note.txt';
    var content = 'engawa fs — 日本語 🌇\nsecond line\ttab';

    assertEqual(await engawa.invoke('fs.exists', { path: file }), false, 'file absent before write');
    assertEqual(await engawa.invoke('fs.writeTextFile', { path: file, contents: content }), null, 'write returns null');
    assertEqual(await engawa.invoke('fs.exists', { path: file }), true, 'file present after write');
    assertEqual(await engawa.invoke('fs.readTextFile', { path: file }), content, 'contents round-trip');

    var st = await engawa.invoke('fs.stat', { path: file });
    assertEqual(st.type, 'file', 'stat type');
    assert(typeof st.size === 'number' && st.size > 0, 'stat size is a positive number');
    assert(typeof st.modified === 'number', 'stat modified is a number');

    await engawa.invoke('fs.remove', { path: dir, recursive: true });
    assertEqual(await engawa.invoke('fs.exists', { path: file }), false, 'gone after recursive remove');
  });

  test('fs.readTextFile on a missing file rejects ENOENT', async function (engawa) {
    var base = await engawa.invoke('path.temp');
    var missing = base + '/engawa-missing-' + Date.now() + '-' + Math.floor(Math.random() * 1e6);
    var err = null;
    try { await engawa.invoke('fs.readTextFile', { path: missing }); } catch (e) { err = e; }
    assert(err, 'expected rejection');
    assertEqual(err.code, 'ENOENT', 'code');
  });

  test('fs.writeTextFile into a missing parent rejects ENOENT', async function (engawa) {
    var base = await engawa.invoke('path.appData');
    var file = base + '/no-such-dir-' + Date.now() + '/x.txt';
    var err = null;
    try { await engawa.invoke('fs.writeTextFile', { path: file, contents: 'x' }); } catch (e) { err = e; }
    assert(err, 'expected rejection');
    assertEqual(err.code, 'ENOENT', 'code');
  });

  test('fs.mkdir non-recursive on an existing path rejects EEXIST', async function (engawa) {
    var dir = await scratchDir(engawa);
    var err = null;
    try { await engawa.invoke('fs.mkdir', { path: dir }); } catch (e) { err = e; }
    assert(err, 'expected rejection');
    assertEqual(err.code, 'EEXIST', 'code');
    await engawa.invoke('fs.remove', { path: dir, recursive: true });
  });

  test('fs.remove on a non-empty dir without recursive rejects ENOTEMPTY', async function (engawa) {
    var dir = await scratchDir(engawa);
    await engawa.invoke('fs.writeTextFile', { path: dir + '/a.txt', contents: 'a' });
    var err = null;
    try { await engawa.invoke('fs.remove', { path: dir }); } catch (e) { err = e; }
    assert(err, 'expected rejection');
    assertEqual(err.code, 'ENOTEMPTY', 'code');
    await engawa.invoke('fs.remove', { path: dir, recursive: true });
  });

  test('fs.readTextFile with a relative path rejects EINVAL', async function (engawa) {
    var err = null;
    try { await engawa.invoke('fs.readTextFile', { path: 'relative/notes.txt' }); } catch (e) { err = e; }
    assert(err, 'expected rejection');
    assertEqual(err.code, 'EINVAL', 'code');
  });

  test('fs.readTextFile on a directory rejects EISDIR', async function (engawa) {
    var dir = await scratchDir(engawa);
    var err = null;
    try { await engawa.invoke('fs.readTextFile', { path: dir }); } catch (e) { err = e; }
    assert(err, 'expected rejection');
    assertEqual(err.code, 'EISDIR', 'code');
    await engawa.invoke('fs.remove', { path: dir, recursive: true });
  });

  test('fs.readDir on a file rejects ENOTDIR', async function (engawa) {
    var dir = await scratchDir(engawa);
    var file = dir + '/a.txt';
    await engawa.invoke('fs.writeTextFile', { path: file, contents: 'x' });
    var err = null;
    try { await engawa.invoke('fs.readDir', { path: file }); } catch (e) { err = e; }
    assert(err, 'expected rejection');
    assertEqual(err.code, 'ENOTDIR', 'code');
    await engawa.invoke('fs.remove', { path: dir, recursive: true });
  });

  test('fs.writeTextFile without contents rejects EINVAL', async function (engawa) {
    var dir = await scratchDir(engawa);
    var err = null;
    try { await engawa.invoke('fs.writeTextFile', { path: dir + '/x.txt' }); } catch (e) { err = e; }
    assert(err, 'expected rejection');
    assertEqual(err.code, 'EINVAL', 'code');
    await engawa.invoke('fs.remove', { path: dir, recursive: true });
  });

  test('fs.readDir lists created entries with isDirectory flags', async function (engawa) {
    var dir = await scratchDir(engawa);
    await engawa.invoke('fs.writeTextFile', { path: dir + '/file.txt', contents: 'x' });
    await engawa.invoke('fs.mkdir', { path: dir + '/sub' });

    var entries = await engawa.invoke('fs.readDir', { path: dir });
    assert(Array.isArray(entries) && entries.length === 2, 'two entries');
    var byName = {};
    entries.forEach(function (e) { byName[e.name] = e.isDirectory; });
    assertEqual(byName['file.txt'], false, 'file flagged not-dir');
    assertEqual(byName['sub'], true, 'sub flagged dir');

    await engawa.invoke('fs.remove', { path: dir, recursive: true });
  });
})();
