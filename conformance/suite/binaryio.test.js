'use strict';
// Binary I/O channel (contract §5a, §11). fs.openWrite/openRead mint single-use app://io
// tokens; the payload rides the scheme handler (a fetch), never the message channel. The
// fetch runs in-page, so this needs a real host — the rendererless mock host skips it.

;(function () {
  var h = (typeof module !== 'undefined' && module.exports) ? require('../harness') : globalThis.engawaConformance;
  var test = h.test, assert = h.assert, assertEqual = h.assertEqual;
  var crypto = (typeof require !== 'undefined') ? require('crypto') : null;

  // §11 names 1 KiB / 10 MiB / 100 MiB. 100 MiB is the mechanism's ceiling; through the
  // base64 control-channel bridge it is impractically slow (a real app's fetch streams it
  // natively, no bridge), so the automated suite covers 1 KiB / 1 MiB / 10 MiB.
  var SIZES = [1024, 1024 * 1024, 10 * 1024 * 1024];

  test('§5a binary write → read round-trips byte-for-byte (1 KiB / 1 MiB / 10 MiB)', async function (engawa) {
    if (!engawa.ioPut) return;   // requires a real host + in-page fetch
    var base = await engawa.invoke('path.temp');
    for (var i = 0; i < SIZES.length; i++) {
      var size = SIZES[i];
      var file = base + '/engawa-io-' + Date.now() + '-' + size + '.bin';
      var data = crypto.randomBytes(size);

      var w = await engawa.invoke('fs.openWrite', { path: file });
      assert(typeof w.url === 'string' && w.url.indexOf('app://io/') === 0, 'openWrite returns an app://io url');
      var put = await engawa.ioPut(w.url, data);
      assert(put.ok === true && put.value.bytesWritten === size, 'PUT wrote ' + size + ' bytes');

      var r = await engawa.invoke('fs.openRead', { path: file });
      var got = await engawa.ioGet(r.url);
      assert(Buffer.compare(got, data) === 0, size + ' bytes round-trip byte-for-byte');

      await engawa.invoke('fs.remove', { path: file });
    }
  });

  test('§5a an io token is single-use', async function (engawa) {
    if (!engawa.ioPut) return;
    var base = await engawa.invoke('path.temp');
    var file = base + '/engawa-io-once-' + Date.now() + '.bin';
    var w = await engawa.invoke('fs.openWrite', { path: file });
    assert((await engawa.ioPut(w.url, Buffer.from('once'))).ok === true, 'first PUT ok');
    assert((await engawa.ioPut(w.url, Buffer.from('again'))).ok === false, 'second PUT on the consumed token fails');
    await engawa.invoke('fs.remove', { path: file });
  });

  test('fs.openRead on a missing file rejects ENOENT', async function (engawa) {
    if (!engawa.ioPut) return;
    var base = await engawa.invoke('path.temp');
    var err = null;
    try { await engawa.invoke('fs.openRead', { path: base + '/engawa-missing-' + Date.now() }); } catch (e) { err = e; }
    assert(err, 'expected rejection');
    assertEqual(err.code, 'ENOENT', 'code');
  });
})();
