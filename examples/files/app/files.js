'use strict';
// Engawa Files — a minimal file browser/viewer. Exercises fs (readDir/stat/readTextFile and
// the §5a binary channel via openRead/openWrite), dialog, clipboard, and path — a different
// surface from the sqlite notes example. In autotest mode it self-checks those and reports.

(function () {
  var engawa = window.engawa;

  // ---- interactive browser ----
  var cwd = null;

  function parent(dir) { return dir.replace(/\/[^/]*$/, '') || '/'; }

  async function list(dir) {
    cwd = dir;
    document.getElementById('cwd').textContent = dir;
    document.getElementById('view').textContent = '';
    var entries;
    try {
      entries = await engawa.invoke('fs.readDir', { path: dir });
    } catch (err) {
      document.getElementById('view').textContent = 'cannot read ' + dir + ': ' + err.message;
      return;
    }
    entries.sort(function (a, b) { return (b.isDirectory - a.isDirectory) || a.name.localeCompare(b.name); });
    var ul = document.getElementById('list');
    ul.textContent = '';
    entries.forEach(function (entry) {
      var li = document.createElement('li');
      li.textContent = (entry.isDirectory ? '📁 ' : '📄 ') + entry.name;
      li.addEventListener('click', function () { openEntry(dir.replace(/\/$/, '') + '/' + entry.name, entry.isDirectory); });
      ul.appendChild(li);
    });
  }

  async function openEntry(path, isDirectory) {
    if (isDirectory) { list(path); return; }
    var view = document.getElementById('view');
    var st = await engawa.invoke('fs.stat', { path: path });
    if (st.size > 1024 * 1024) { view.textContent = '(' + st.size + ' bytes — too large to preview)'; return; }
    try {
      view.textContent = await engawa.invoke('fs.readTextFile', { path: path });
    } catch (err) {
      view.textContent = '(binary or unreadable, ' + st.size + ' bytes)';
    }
  }

  function wireUI() {
    document.getElementById('up').addEventListener('click', function () { if (cwd) list(parent(cwd)); });
    document.getElementById('open').addEventListener('click', async function () {
      var r = await engawa.invoke('dialog.open', {});
      if (!r.canceled && r.paths.length > 0) openEntry(r.paths[0], false);
    });
    document.getElementById('copy').addEventListener('click', function () {
      if (cwd) engawa.invoke('clipboard.writeText', { text: cwd });
    });
  }

  async function start() {
    wireUI();
    list(await engawa.invoke('path.home'));
  }

  // ---- autotest: exercise fs (incl. §5a binary), clipboard, path ----
  async function autotest() {
    var checks = {};
    try {
      var appData = await engawa.invoke('path.appData');
      var file = appData + '/probe.txt';
      var content = 'engawa files — 日本語 📁';

      await engawa.invoke('fs.writeTextFile', { path: file, contents: content });
      checks.readText = (await engawa.invoke('fs.readTextFile', { path: file })) === content;

      // §5a binary READ from a real app: openRead → fetch(app://io/<token>)
      var rd = await engawa.invoke('fs.openRead', { path: file });
      checks.binaryRead = (await fetch(rd.url).then(function (r) { return r.text(); })) === content;

      // §5a binary WRITE: openWrite → fetch PUT bytes
      var bin = appData + '/probe.bin';
      var payload = new TextEncoder().encode('binary-payload-🌇');
      var wr = await engawa.invoke('fs.openWrite', { path: bin });
      var put = await fetch(wr.url, { method: 'PUT', body: payload }).then(function (r) { return r.json(); });
      checks.binaryWrite = put.ok === true && put.value.bytesWritten === payload.length;

      checks.readDir = (await engawa.invoke('fs.readDir', { path: appData })).some(function (x) { return x.name === 'probe.txt'; });

      await engawa.invoke('clipboard.writeText', { text: file });
      checks.clipboard = (await engawa.invoke('clipboard.readText')) === file;
    } catch (err) {
      checks.error = String((err && err.message) || err);
    }

    var ok = ['readText', 'binaryRead', 'binaryWrite', 'readDir', 'clipboard'].every(function (k) { return checks[k] === true; });
    var appData2 = await engawa.invoke('path.appData');
    await engawa.invoke('fs.writeTextFile', {
      path: appData2 + '/autotest-result.json',
      contents: JSON.stringify({ ok: ok, checks: checks, final: ok ? 'PASS' : 'FAIL' }),
    });
    await engawa.invoke('app.__exit', { code: ok ? 0 : 1 });
  }

  if (window.__engawaAutotest) autotest(); else start();
})();
