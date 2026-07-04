'use strict';
// Dev signing helper for the make-notes gate (contract §7.1). The keypair stands in for the
// real publisher key; the .app embeds the public key (trust-root.txt), the gate signs the
// app-update payload with the private key.
//
//   node crypto.js keygen <dir>              → writes <dir>/priv.pem + <dir>/trust-root.txt (raw pubkey, base64)
//   node crypto.js sign <priv.pem> <file>    → prints {"hash","signature"} (sha256 hex + ed25519 base64)

const crypto = require('crypto');
const fs = require('fs');

const cmd = process.argv[2];

if (cmd === 'keygen') {
  const dir = process.argv[3];
  const { publicKey, privateKey } = crypto.generateKeyPairSync('ed25519');
  fs.writeFileSync(dir + '/priv.pem', privateKey.export({ type: 'pkcs8', format: 'pem' }));
  const trust = Buffer.from(publicKey.export({ format: 'jwk' }).x, 'base64url').toString('base64');
  fs.writeFileSync(dir + '/trust-root.txt', trust);
  process.stdout.write(trust);
} else if (cmd === 'sign') {
  const priv = crypto.createPrivateKey(fs.readFileSync(process.argv[3]));
  const digest = crypto.createHash('sha256').update(fs.readFileSync(process.argv[4])).digest();
  const signature = crypto.sign(null, digest, priv).toString('base64');
  process.stdout.write(JSON.stringify({ hash: digest.toString('hex'), signature }));
} else {
  process.stderr.write('usage: crypto.js keygen <dir> | sign <priv.pem> <file>\n');
  process.exit(2);
}
