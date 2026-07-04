import { createHash, createPrivateKey, generateKeyPairSync, sign as edSign } from "node:crypto";
import { readFileSync } from "node:fs";
import { CliError } from "./log.ts";

// Dev signing keypair (contract §7.1). The trust root is the raw 32-byte ed25519 public key,
// base64 — exactly what the host embeds and verifies against.
export interface Keypair {
  trustRoot: string;
  privatePem: string;
}

export function generateKeypair(): Keypair {
  const { publicKey, privateKey } = generateKeyPairSync("ed25519");
  const jwk = publicKey.export({ format: "jwk" }) as { x?: string };
  if (jwk.x === undefined) throw new CliError("failed to export the ed25519 public key");
  return {
    trustRoot: Buffer.from(jwk.x, "base64url").toString("base64"),
    privatePem: privateKey.export({ type: "pkcs8", format: "pem" }).toString(),
  };
}

// Sign an app-update payload: the signature is over the payload's sha256 content hash (§7.1).
export interface Signature {
  hash: string;
  signature: string;
}

export function signPayload(payloadPath: string, privatePem: string): Signature {
  const digest = createHash("sha256").update(readFileSync(payloadPath)).digest();
  const signature = edSign(null, digest, createPrivateKey(privatePem)).toString("base64");
  return { hash: digest.toString("hex"), signature };
}
