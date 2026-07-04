'use strict';
// macOS reference-host driver for the conformance runner.
//
// Part 2 of the vertical slice builds this out: launch the Swift host in a headless
// conformance mode, load the suite into an app:// test page against the real in-page
// shell.js, and expose an `engawa`-shaped proxy (or collect the in-page report) so the
// runner drives the same assertions it runs on the mock host.

function connectMacosHost() {
  throw new Error('macOS host not built yet (bootstrap stage 2 part 2)');
}

module.exports = { connectMacosHost };
