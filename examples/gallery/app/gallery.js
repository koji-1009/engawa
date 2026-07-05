'use strict';
// Engawa Gallery — a manual feature check. Each button calls the real engawa API and logs the
// result; the dialog / notification / shellOpen buttons drive real native UI (the paths the
// conformance suite can only substitute). External script (CSP: script-src app:), no inline handlers.

(function () {
  var engawa = window.engawa;
  var out = document.getElementById('log');

  function log(label, value, isErr) {
    var div = document.createElement('div');
    div.className = isErr ? 'entry err' : 'entry';
    var text = typeof value === 'string' ? value : JSON.stringify(value);
    div.textContent = new Date().toLocaleTimeString() + '  ' + label + ' → ' + text;
    out.insertBefore(div, out.firstChild);
  }

  // Invoke a command, log its result (or its error code/message), and return the value.
  async function call(label, cmd, args) {
    try {
      var r = await engawa.invoke(cmd, args);
      log(label, r === undefined || r === null ? 'ok' : r);
      return r;
    } catch (e) {
      log(label, (e && e.code ? e.code + ': ' : '') + ((e && e.message) || String(e)), true);
    }
  }

  function on(id, fn) {
    var el = document.getElementById(id);
    if (el) el.addEventListener('click', fn);
  }

  // dialog (real native pickers) ----------------------------------------------------------
  on('dlg-open', function () { call('dialog.open', 'dialog.open', { title: 'Pick a file' }); });
  on('dlg-open-multi', function () { call('dialog.open multiple', 'dialog.open', { title: 'Pick files', multiple: true }); });
  on('dlg-open-dir', function () { call('dialog.open folder', 'dialog.open', { title: 'Pick a folder', directory: true }); });
  on('dlg-save', function () { call('dialog.save', 'dialog.save', { title: 'Save as', defaultName: 'engawa.txt' }); });
  on('dlg-msg', function () { call('dialog.message', 'dialog.message', { title: 'Engawa', message: 'Hello from Engawa!' }); });
  on('dlg-msg-buttons', function () { call('dialog.message buttons (returns index)', 'dialog.message', { title: 'Choose', message: 'Pick one:', buttons: ['Yes', 'No', 'Maybe'] }); });

  // notification (real toast) -------------------------------------------------------------
  on('notify', function () { call('notification.show', 'notification.show', { title: 'Engawa', body: 'This is a real system toast.' }); });

  // clipboard -----------------------------------------------------------------------------
  on('clip-write', function () { call('clipboard.writeText', 'clipboard.writeText', { text: document.getElementById('clip-text').value }); });
  on('clip-read', async function () {
    var r = await call('clipboard.readText', 'clipboard.readText');
    if (typeof r === 'string') document.getElementById('clip-text').value = r;
  });

  // fs + sqlite ---------------------------------------------------------------------------
  on('fs-write', async function () {
    var appData = await engawa.invoke('path.appData');
    await call('fs.writeTextFile', 'fs.writeTextFile', { path: appData + '/gallery.txt', contents: 'written at ' + new Date().toISOString() });
  });
  on('fs-read', async function () {
    var appData = await engawa.invoke('path.appData');
    await call('fs.readTextFile', 'fs.readTextFile', { path: appData + '/gallery.txt' });
  });
  on('sql-run', async function () {
    try {
      var appData = await engawa.invoke('path.appData');
      var db = (await engawa.invoke('sqlite.open', { path: appData + '/gallery.db' })).db;
      await engawa.invoke('sqlite.execute', { db: db, sql: 'CREATE TABLE IF NOT EXISTS t(id INTEGER PRIMARY KEY, v TEXT)' });
      await engawa.invoke('sqlite.execute', { db: db, sql: 'INSERT INTO t(v) VALUES(?)', params: ['row @ ' + new Date().toLocaleTimeString()] });
      var rows = (await engawa.invoke('sqlite.query', { db: db, sql: 'SELECT id, v FROM t ORDER BY id DESC LIMIT 5' })).rows;
      await engawa.invoke('sqlite.close', { db: db });
      log('sqlite last 5 rows', rows);
    } catch (e) {
      log('sqlite', (e && e.message) || String(e), true);
    }
  });

  // window --------------------------------------------------------------------------------
  on('win-title', function () { call('window.setTitle', 'window.setTitle', { title: 'Engawa Gallery @ ' + new Date().toLocaleTimeString() }); });
  on('win-size', function () { call('window.setSize', 'window.setSize', { width: 900, height: 640 }); });
  on('win-min', function () { call('window.minimize', 'window.minimize'); });
  on('win-max', function () { call('window.maximize', 'window.maximize'); });

  // shellOpen (opens the real browser) ----------------------------------------------------
  on('open-url', function () { call('shellOpen.openExternal', 'shellOpen.openExternal', { url: document.getElementById('url').value }); });

  // info ----------------------------------------------------------------------------------
  on('paths', async function () {
    log('path.appData', await engawa.invoke('path.appData'));
    log('path.home', await engawa.invoke('path.home'));
    log('path.temp', await engawa.invoke('path.temp'));
  });
  on('app-info', async function () {
    log('app.version', await engawa.invoke('app.version'));
    log('app.engineInfo', await engawa.invoke('app.engineInfo'));
    log('update.status', await engawa.invoke('update.status'));
  });

  // live events ---------------------------------------------------------------------------
  ['window.resize', 'window.focus', 'window.blur'].forEach(function (topic) {
    engawa.on(topic, function (p) { log('event ' + topic, p == null ? '(no payload)' : p); });
  });

  document.getElementById('platform').textContent = engawa.platform + ' · contract ' + engawa.contractVersion;
  log('ready', 'click a button above');
})();
