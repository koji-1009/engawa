// Optional typed wrapper over the `update` namespace (adapters/update/spec.md). App code:
//
//   import { update } from '<update-adapter>/js/index.js';
//   const up = update(engawa);
//   up.onReadyToInstall(({ version }) => promptUser(version));
//   await up.confirmBoot();                 // once the app has initialized on this launch
//   const { mode } = await up.evaluate(manifest, { contractProvided, capabilities });
//
// Trust (§7.1) and the slot swap (§8) are host obligations; this only wraps the wire.

export function update(engawa) {
  return {
    status: () => engawa.invoke('update.status'),
    evaluate: (manifest, provided) => engawa.invoke('update.evaluate', { manifest, provided }),
    stageAppUpdate: (payloadPath, hash, signature, version) =>
      engawa.invoke('update.stageAppUpdate', { payloadPath, hash, signature, version }),
    confirmBoot: () => engawa.invoke('update.confirmBoot'),
    install: () => engawa.invoke('update.install'),
    onReadyToInstall: (handler) => engawa.on('update.readyToInstall', handler),
  };
}
