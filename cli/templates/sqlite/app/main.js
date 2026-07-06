'use strict';
// A notes app backed by the sqlite adapter (declared in engawa.json). Notes live in a real
// database under path.appData/notes.db. External script only — inline script is dead under the
// default CSP (§7.3).
(function () {
  var engawa = window.engawa;

  async function openDB() {
    var appData = await engawa.invoke('path.appData');
    var db = (await engawa.invoke('sqlite.open', { path: appData + '/notes.db' })).db;
    await engawa.invoke('sqlite.execute', { db: db, sql: 'CREATE TABLE IF NOT EXISTS notes(id INTEGER PRIMARY KEY, body TEXT)' });
    return db;
  }

  async function listNotes() {
    var db = await openDB();
    var rows = (await engawa.invoke('sqlite.query', { db: db, sql: 'SELECT id, body FROM notes ORDER BY id' })).rows;
    await engawa.invoke('sqlite.close', { db: db });
    return rows;
  }

  async function render() {
    var rows = await listNotes();
    var ul = document.getElementById('notes');
    ul.textContent = '';
    rows.forEach(function (r) { var li = document.createElement('li'); li.textContent = r.body; ul.appendChild(li); });
    document.getElementById('status').textContent = rows.length + ' note(s)';
  }

  document.getElementById('add').addEventListener('submit', async function (ev) {
    ev.preventDefault();
    var body = document.getElementById('body').value.trim();
    if (!body) return;
    var db = await openDB();
    await engawa.invoke('sqlite.execute', { db: db, sql: 'INSERT INTO notes(body) VALUES(?)', params: [body] });
    await engawa.invoke('sqlite.close', { db: db });
    document.getElementById('body').value = '';
    render();
  });

  render();
})();
