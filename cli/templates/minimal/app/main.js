'use strict';
// A minimal notes app backed by the built-in fs namespace — durable data (contract §10), no
// adapters needed, so it builds and runs out of the box. To use sqlite instead, declare the
// adapter in engawa.json's "adapters" (see the sqlite template). External script only (inline
// script is dead under the default CSP, §7.3).
(function () {
  var engawa = window.engawa;
  var file;
  async function load() {
    if (!file) file = (await engawa.invoke('path.appData')) + '/notes.json';
    try { return JSON.parse(await engawa.invoke('fs.readTextFile', { path: file })); }
    catch (e) { return []; }
  }
  async function save(notes) { await engawa.invoke('fs.writeTextFile', { path: file, contents: JSON.stringify(notes) }); }
  async function render() {
    var notes = await load();
    var ul = document.getElementById('notes');
    ul.textContent = '';
    notes.forEach(function (n) { var li = document.createElement('li'); li.textContent = n; ul.appendChild(li); });
  }
  document.getElementById('add').addEventListener('submit', async function (ev) {
    ev.preventDefault();
    var body = document.getElementById('body').value.trim();
    if (!body) return;
    var notes = await load();
    notes.push(body);
    await save(notes);
    document.getElementById('body').value = '';
    render();
  });
  render();
})();
