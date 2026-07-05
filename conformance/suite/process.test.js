'use strict';
// process namespace (spec/commands/process.md). Spawns the declared echo sidecar, exercises
// the pull-stream protocol and its events, the firehose volume, and §7.2/ESRCH errors.

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert, assertEqual = h.assertEqual;

  function delay(ms) { return new Promise(function (r) { setTimeout(r, ms); }); }
  async function waitFor(cond, timeoutMs) {
    var start = Date.now();
    while (!cond()) {
      if (Date.now() - start > timeoutMs) throw new Error('timed out');
      await delay(10);
    }
  }
  async function drain(engawa, pid) {
    var out = '', eof = false, deadline = Date.now() + 5000;
    while (!eof && Date.now() < deadline) {
      var r = await engawa.invoke('process.read', { pid: pid, stream: 'stdout', maxBytes: 65536 });
      out += r.data; eof = r.eof;
      if (!r.data && !eof) await delay(10);
    }
    return { out: out, eof: eof };
  }

  test('spawn → stdinWrite → read echoes input; kill → process.exit', async function (engawa) {
    var readable = [], exits = [];
    var offR = engawa.on('process.readable', function (p) { readable.push(p); });
    var offE = engawa.on('process.exit', function (p) { exits.push(p); });

    var spawned = await engawa.invoke('process.spawn', { command: './bin/echo-sidecar' });
    var pid = spawned.pid;
    assert(typeof pid === 'number', 'spawn returns a numeric pid');

    await engawa.invoke('process.stdinWrite', { pid: pid, data: 'hello えんがわ\n' });

    var out = '', deadline = Date.now() + 3000;
    while (out.indexOf('hello えんがわ') < 0 && Date.now() < deadline) {
      var r = await engawa.invoke('process.read', { pid: pid, stream: 'stdout', maxBytes: 65536 });
      out += r.data;
      if (!r.data) await delay(20);
    }
    assert(out.indexOf('hello えんがわ') >= 0, 'stdout echoed stdin; got ' + JSON.stringify(out));
    assert(readable.some(function (e) { return e.pid === pid && e.stream === 'stdout'; }), 'a readable event fired');

    await engawa.invoke('process.kill', { pid: pid });
    await drain(engawa, pid);   // drain to EOF so exit can fire (§4.1)
    await waitFor(function () { return exits.some(function (e) { return e.pid === pid; }); }, 3000);
    offR(); offE();
    assert(exits.some(function (e) { return e.pid === pid && typeof e.code === 'number'; }), 'process.exit carried a code');
  });

  test('process.stdinClose lets a read-to-EOF sidecar finish; post-exit ops are no-ops', async function (engawa) {
    var exits = [];
    var offE = engawa.on('process.exit', function (p) { exits.push(p); });
    var spawned = await engawa.invoke('process.spawn', { command: './bin/echo-sidecar' });
    var pid = spawned.pid;
    await engawa.invoke('process.stdinWrite', { pid: pid, data: 'bye\n' });
    await engawa.invoke('process.stdinClose', { pid: pid });   // sidecar sees stdin end → exit 0
    await drain(engawa, pid);
    await waitFor(function () { return exits.some(function (e) { return e.pid === pid; }); }, 3000);
    offE();
    assert(exits.some(function (e) { return e.pid === pid; }), 'process.exit fired after stdinClose');
    assertEqual(await engawa.invoke('process.kill', { pid: pid }), null, 'kill after exit is a no-op, not ESRCH');
    var r = await engawa.invoke('process.read', { pid: pid, stream: 'stdout', maxBytes: 16 });
    assertEqual(r.eof, true, 'read after exit reports eof');
  });

  test('process.read makes forward progress when maxBytes < the next character', async function (engawa) {
    var spawned = await engawa.invoke('process.spawn', { command: './bin/echo-sidecar' });
    var pid = spawned.pid;
    await engawa.invoke('process.stdinWrite', { pid: pid, data: 'あ' });   // one 3-byte character
    var got = '', deadline = Date.now() + 3000;
    while (got.length === 0 && Date.now() < deadline) {
      var r = await engawa.invoke('process.read', { pid: pid, stream: 'stdout', maxBytes: 1 });
      got += r.data;
      if (!r.data) await delay(20);
    }
    assertEqual(got, 'あ', 'a 1-byte read still returned the whole 3-byte character (no livelock)');
    await engawa.invoke('process.kill', { pid: pid });
    await drain(engawa, pid);
  });

  test('process.read rejects a bad stream or negative maxBytes with EINVAL', async function (engawa) {
    var spawned = await engawa.invoke('process.spawn', { command: './bin/echo-sidecar' });
    var pid = spawned.pid;
    for (var i = 0; i < 2; i++) {
      var call = i === 0 ? { pid: pid, stream: 'stdlog' } : { pid: pid, stream: 'stdout', maxBytes: -1 };
      var err = null;
      try { await engawa.invoke('process.read', call); } catch (e) { err = e; }
      assert(err && err.code === 'EINVAL', 'expected EINVAL');
    }
    await engawa.invoke('process.kill', { pid: pid });
    await drain(engawa, pid);
  });

  test('firehose: chunked reads drain the full volume, then exit', async function (engawa) {
    var N = 2 * 1024 * 1024;   // 2 MiB of ASCII 'a'
    var exits = [];
    var offE = engawa.on('process.exit', function (p) { exits.push(p); });

    var spawned = await engawa.invoke('process.spawn', { command: './bin/echo-sidecar', args: ['firehose:' + N] });
    var pid = spawned.pid;

    var total = 0, eof = false, deadline = Date.now() + 15000;
    while (!eof && Date.now() < deadline) {
      var r = await engawa.invoke('process.read', { pid: pid, stream: 'stdout', maxBytes: 65536 });
      total += r.data.length;   // ASCII, so char length == byte length
      eof = r.eof;
      if (!r.data && !eof) await delay(5);
    }
    await waitFor(function () { return exits.some(function (e) { return e.pid === pid; }); }, 3000);
    offE();
    assertEqual(total, N, 'received the full firehose volume');
  });

  test('process.spawn of an undeclared command → EPERM (§7.2)', async function (engawa) {
    var err = null;
    try { await engawa.invoke('process.spawn', { command: '/bin/ls' }); } catch (e) { err = e; }
    assert(err, 'expected rejection');
    assertEqual(err.code, 'EPERM', 'code');
  });

  test('process.spawn escaping the bundle → EPERM (§7.2)', async function (engawa) {
    var err = null;
    try { await engawa.invoke('process.spawn', { command: '../../../../bin/ls' }); } catch (e) { err = e; }
    assert(err, 'expected rejection');
    assertEqual(err.code, 'EPERM', 'code');
  });

  test('process.read/stdinWrite/kill on an unknown pid → ESRCH', async function (engawa) {
    for (const call of [
      ['process.read', { pid: 999999, stream: 'stdout', maxBytes: 16 }],
      ['process.stdinWrite', { pid: 999999, data: 'x' }],
      ['process.kill', { pid: 999999 }],
    ]) {
      var err = null;
      try { await engawa.invoke(call[0], call[1]); } catch (e) { err = e; }
      assert(err, 'expected rejection from ' + call[0]);
      assertEqual(err.code, 'ESRCH', call[0] + ' code');
    }
  });
})();
