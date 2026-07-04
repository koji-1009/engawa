'use strict';
// Engawa Notes — a minimal notes app over the sqlite adapter, update-enabled.
// In autotest mode (window.__engawaAutotest, set by ENGAWA_AUTOTEST=1) it self-drives the
// make-notes acceptance sequence across launches, reporting via exit code + a result file.
// The app version rides index.html's <meta app-version>, so the same notes.js serves both
// the initial app (version 1) and the update payload (version 2).

(function () {
  var engawa = window.engawa;
  var APP_VERSION = document.querySelector('meta[name="app-version"]').content;

  function dbFile(appData) { return appData + '/notes.db'; }

  async function openDB() {
    var appData = await engawa.invoke('path.appData');
    var db = (await engawa.invoke('sqlite.open', { path: dbFile(appData) })).db;
    await engawa.invoke('sqlite.execute', { db: db, sql: 'CREATE TABLE IF NOT EXISTS notes(id INTEGER PRIMARY KEY, body TEXT)' });
    return db;
  }

  async function listNotes() {
    var db = await openDB();
    var rows = (await engawa.invoke('sqlite.query', { db: db, sql: 'SELECT id, body FROM notes ORDER BY id' })).rows;
    await engawa.invoke('sqlite.close', { db: db });
    return rows;
  }

  // ---- normal interactive mode ----
  async function runUI() {
    document.getElementById('status').textContent = 'Engawa Notes v' + APP_VERSION;
    async function render() {
      var rows = await listNotes();
      var ul = document.getElementById('notes');
      ul.textContent = '';
      rows.forEach(function (r) { var li = document.createElement('li'); li.textContent = r.body; ul.appendChild(li); });
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
  }

  // ---- autotest state machine (make notes gate) ----
  async function runAutotest(auto) {
    var appData = await engawa.invoke('path.appData');
    var stepFile = appData + '/autotest-step';
    var resultFile = appData + '/autotest-result.json';
    var NOTE = 'engawa-durable-note';

    async function step() {
      try { return parseInt(await engawa.invoke('fs.readTextFile', { path: stepFile }), 10) || 0; } catch (e) { return 0; }
    }
    async function setStep(n) { await engawa.invoke('fs.writeTextFile', { path: stepFile, contents: String(n) }); }
    async function done(obj, code) {
      obj.appVersion = APP_VERSION;
      await engawa.invoke('fs.writeTextFile', { path: resultFile, contents: JSON.stringify(obj) });
      await engawa.invoke('app.__exit', { code: code });
    }

    try {
      var s = await step();
      if (s === 0) {
        // write a durable record
        var db = await openDB();
        await engawa.invoke('sqlite.execute', { db: db, sql: 'INSERT INTO notes(body) VALUES(?)', params: [NOTE] });
        await engawa.invoke('sqlite.close', { db: db });
        await setStep(1);
        await done({ step: 0, ok: true }, 0);
      } else if (s === 1) {
        // record must have survived the quit/relaunch; then stage the signed app-update
        var rows = await listNotes();
        var persisted = rows.length === 1 && rows[0].body === NOTE;
        await engawa.invoke('update.stageAppUpdate', {
          payloadPath: auto.update.payloadPath, hash: auto.update.hash,
          signature: auto.update.signature, version: auto.update.version,
        });
        await setStep(2);
        await done({ step: 1, ok: persisted, persisted: persisted }, persisted ? 0 : 1);
      } else if (s === 2) {
        // this launch boots the pending (updated) slot; adopt it and confirm data survived
        await engawa.invoke('update.confirmBoot');
        var rows2 = await listNotes();
        var dataIntact = rows2.length === 1 && rows2[0].body === NOTE;
        var updated = APP_VERSION === '2';
        await setStep(3);
        await done({ step: 2, ok: dataIntact && updated, dataIntact: dataIntact, updated: updated,
                     final: (dataIntact && updated) ? 'PASS' : 'FAIL' }, (dataIntact && updated) ? 0 : 1);
      } else {
        await done({ step: s, ok: false, error: 'unexpected step' }, 1);
      }
    } catch (err) {
      await done({ ok: false, error: String((err && err.message) || err) }, 1);
    }
  }

  if (window.__engawaAutotest) { runAutotest(window.__engawaAutotest); } else { runUI(); }
})();
