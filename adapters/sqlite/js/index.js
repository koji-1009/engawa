// Optional typed wrapper over the `sqlite` namespace (adapters/sqlite/spec.md). App code:
//
//   import { openDatabase } from '<sqlite-adapter>/js/index.js';
//   const db = await openDatabase(engawa, await engawa.invoke('path.appData') + '/app.db');
//   await db.execute('CREATE TABLE IF NOT EXISTS notes(id INTEGER PRIMARY KEY, body TEXT)');
//   await db.execute('INSERT INTO notes(body) VALUES(?)', ['hi']);
//   const rows = await db.query('SELECT * FROM notes');
//   await db.close();
//
// The adapter author never touches IPC; this is a thin convenience over engawa.invoke.

export async function openDatabase(engawa, path) {
  const { db } = await engawa.invoke('sqlite.open', { path });
  return {
    db,
    execute: (sql, params) => engawa.invoke('sqlite.execute', { db, sql, params }),
    query: (sql, params) => engawa.invoke('sqlite.query', { db, sql, params }).then((r) => r.rows),
    close: () => engawa.invoke('sqlite.close', { db }),
  };
}
