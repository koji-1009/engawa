'use strict';
// Conformance for the sqlite adapter (adapters/sqlite/spec.md). Runs against any host that
// serves the `sqlite` namespace (both the mock host and the macOS host with the adapter
// compiled in); skips where the capability is absent.

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../../../conformance/harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert, assertEqual = h.assertEqual;

  function has(engawa) { return engawa.capabilities.indexOf('sqlite') >= 0; }

  test('sqlite: create → insert → query round-trip (unicode + params)', async function (engawa) {
    if (!has(engawa)) return;
    var db = (await engawa.invoke('sqlite.open', { path: ':memory:' })).db;
    await engawa.invoke('sqlite.execute', { db: db, sql: 'CREATE TABLE notes(id INTEGER PRIMARY KEY, body TEXT)' });
    var ins = await engawa.invoke('sqlite.execute', { db: db, sql: 'INSERT INTO notes(body) VALUES(?)', params: ['えんがわ 🌇'] });
    assertEqual(ins.changes, 1, 'one row inserted');
    assert(ins.lastInsertRowid >= 1, 'a rowid was assigned');

    var q = await engawa.invoke('sqlite.query', { db: db, sql: 'SELECT id, body FROM notes WHERE id = ?', params: [ins.lastInsertRowid] });
    assertEqual(q.rows.length, 1, 'one row selected');
    assertEqual(q.rows[0].body, 'えんがわ 🌇', 'text round-trips');
    assertEqual(q.rows[0].id, ins.lastInsertRowid, 'integer round-trips');

    await engawa.invoke('sqlite.close', { db: db });
  });

  test('sqlite: data persists across close/reopen (durability, contract §10)', async function (engawa) {
    if (!has(engawa)) return;
    var base = await engawa.invoke('path.appData');
    var file = base + '/notes-' + Date.now() + '-' + Math.floor(Math.random() * 1e6) + '.db';

    var db = (await engawa.invoke('sqlite.open', { path: file })).db;
    await engawa.invoke('sqlite.execute', { db: db, sql: 'CREATE TABLE t(x TEXT)' });
    await engawa.invoke('sqlite.execute', { db: db, sql: 'INSERT INTO t VALUES(?)', params: ['durable'] });
    await engawa.invoke('sqlite.close', { db: db });

    db = (await engawa.invoke('sqlite.open', { path: file })).db;
    var q = await engawa.invoke('sqlite.query', { db: db, sql: 'SELECT x FROM t' });
    assertEqual(q.rows[0].x, 'durable', 'value survived close + reopen');
    await engawa.invoke('sqlite.close', { db: db });
    await engawa.invoke('fs.remove', { path: file });
  });

  test('sqlite: unknown db handle → EBADF', async function (engawa) {
    if (!has(engawa)) return;
    var err = null;
    try { await engawa.invoke('sqlite.query', { db: 999999, sql: 'SELECT 1' }); } catch (e) { err = e; }
    assert(err, 'expected rejection');
    assertEqual(err.code, 'EBADF', 'code');
  });

  test('sqlite: malformed SQL → ESQLITE', async function (engawa) {
    if (!has(engawa)) return;
    var db = (await engawa.invoke('sqlite.open', { path: ':memory:' })).db;
    var err = null;
    try { await engawa.invoke('sqlite.execute', { db: db, sql: 'NOT VALID SQL ;;' }); } catch (e) { err = e; }
    assert(err, 'expected rejection');
    assertEqual(err.code, 'ESQLITE', 'code');
    await engawa.invoke('sqlite.close', { db: db });
  });
})();
