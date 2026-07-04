#!/usr/bin/env node
// Engawa conformance runner — entry point for `make conformance`.
//
// Bootstrap stage 1: the harness is wired and reports an honest failure — there is
// no host to test yet. Stage 2 lands the first live assertion executed against BOTH
// the macOS reference host and the Node mock host (contract §11).

'use strict';

function main() {
  console.error('conformance: no host targets wired yet (bootstrap stage 1).');
  console.error('The suite runs against the macOS host and the Node mock host once stage 2 lands.');
  process.exit(1);
}

main();
