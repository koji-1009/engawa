#!/usr/bin/env bash
# The make-notes acceptance gate (bootstrap stage 6). Proves the twin claims in one run:
# data survives (sqlite) and code updates (update). No UI scripting — examples/notes drives
# itself in autotest mode, one phase per launch, reporting via exit code + a result file.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../../.." && pwd)"
BUILD="$REPO/build/notes"
GATE="$REPO/examples/notes/gate"
rm -rf "$BUILD"; mkdir -p "$BUILD"

# ---- Linux edition (C++ + WebKitGTK) ---------------------------------------------------------
# No .app bundle: build the native host and drive it through the environment (the same variables the
# conformance driver uses), one autotest phase per launch. The macOS flow below is left untouched.
if [ "$(uname)" = "Linux" ]; then
  echo "== build host (linux, with sqlite + update adapters) =="
  bash "$REPO/hosts/linux/build.sh" >/dev/null
  HOST_BIN="$REPO/hosts/linux/build/EngawaHost"

  echo "== dev signing keypair (§7.1 trust root) =="
  KEYS="$BUILD/keys"; mkdir -p "$KEYS"
  node "$GATE/crypto.js" keygen "$KEYS" >/dev/null
  TRUST="$(tr -d '[:space:]' < "$KEYS/trust-root.txt")"

  echo "== stage app assets =="
  APP="$BUILD/app"; mkdir -p "$APP"
  cp -R "$REPO/examples/notes/app/." "$APP/"
  SHELLJS="$REPO/shell-js/shell.js"
  BUNDLE="$REPO/examples/notes"   # engawa.json + declared sidecars

  echo "== build + sign app-update payload (version 2) =="
  UPDSRC="$BUILD/update-src"; mkdir -p "$UPDSRC"
  cp -R "$REPO/examples/notes/app/." "$UPDSRC/"
  node -e 'const fs=require("fs");const p=process.argv[1];fs.writeFileSync(p,fs.readFileSync(p,"utf8").replace(/content="1"/,"content=\"2\""))' "$UPDSRC/index.html"
  TAR="$BUILD/app-v2.tar"
  tar -cf "$TAR" -C "$UPDSRC" .
  SIGNED="$(node "$GATE/crypto.js" sign "$KEYS/priv.pem" "$TAR")"
  HASH="$(printf '%s' "$SIGNED" | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>process.stdout.write(JSON.parse(s).hash))')"
  SIG="$(printf '%s' "$SIGNED" | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>process.stdout.write(JSON.parse(s).signature))')"
  UPDATE_JSON="{\"payloadPath\":\"$TAR\",\"hash\":\"$HASH\",\"signature\":\"$SIG\",\"version\":\"2.0.0\"}"

  echo "== run scripted sequence (write → quit → relaunch → read → update → relaunch) =="
  DATA="$BUILD/data"; rm -rf "$DATA"; mkdir -p "$DATA"
  RESULT="$DATA/data/autotest-result.json"
  llaunch() {
    # `|| true`: a failed autotest phase exits non-zero (app.__exit code), and under `set -e` that
    # would abort the gate BEFORE the result dump + PASS/FAIL evaluator below. Swallow the launch
    # status so the evaluator always runs and reports the real failure (or a missing result → FAIL).
    ( export ENGAWA_AUTOTEST=1 ENGAWA_DATA_ROOT="$DATA" ENGAWA_APP_ROOT="$APP" \
             ENGAWA_SHELL_JS="$SHELLJS" ENGAWA_BUNDLE_ROOT="$BUNDLE" ENGAWA_TRUST_ROOT="$TRUST"
      if [ -n "${1:-}" ]; then export ENGAWA_AUTOTEST_UPDATE="$1"; fi
      timeout 60 "$HOST_BIN" ) || true
  }
  echo "-- launch 1: write a record"; llaunch ""
  echo "-- launch 2: relaunch, read it back, stage the signed app-update"; llaunch "$UPDATE_JSON"
  echo "-- launch 3: relaunch onto the updated slot, confirm, verify the record survived"; llaunch ""

  echo "== result =="
  cat "$RESULT"; echo
  node -e 'const r=require(process.argv[1]);if(r.final==="PASS"&&r.dataIntact&&r.updated){console.log("make notes: PASS (data survived sqlite; code updated via update)");process.exit(0)}console.log("make notes: FAIL");process.exit(1)' "$RESULT"
  exit $?
fi

echo "== build host (with sqlite + update adapters) =="
swift build --package-path "$REPO/hosts/macos" >/dev/null
HOST_BIN="$REPO/hosts/macos/.build/debug/EngawaHost"

echo "== dev signing keypair (§7.1 trust root) =="
KEYS="$BUILD/keys"; mkdir -p "$KEYS"
node "$GATE/crypto.js" keygen "$KEYS" >/dev/null

echo "== bundle Notes.app =="
APP="$BUILD/Notes.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/app"
cp "$HOST_BIN" "$APP/Contents/MacOS/EngawaHost"
cp -R "$REPO/examples/notes/app/." "$APP/Contents/Resources/app/"
cp "$REPO/shell-js/shell.js" "$APP/Contents/Resources/shell.js"
cp "$REPO/examples/notes/engawa.json" "$APP/Contents/Resources/engawa.json"
cp "$KEYS/trust-root.txt" "$APP/Contents/Resources/trust-root.txt"
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>EngawaHost</string>
  <key>CFBundleIdentifier</key><string>dev.engawa.notes</string>
  <key>CFBundleName</key><string>Engawa Notes</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>1.0.0</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
codesign -s - --force "$APP" >/dev/null 2>&1   # ad-hoc dev signing
# notarization: stub hook (out of scope until distribution)

echo "== build + sign app-update payload (version 2) =="
UPDSRC="$BUILD/update-src"; mkdir -p "$UPDSRC"
cp -R "$REPO/examples/notes/app/." "$UPDSRC/"
node -e 'const fs=require("fs");const p=process.argv[1];fs.writeFileSync(p,fs.readFileSync(p,"utf8").replace(/content="1"/,"content=\"2\""))' "$UPDSRC/index.html"
TAR="$BUILD/app-v2.tar"
tar -cf "$TAR" -C "$UPDSRC" .
SIGNED="$(node "$GATE/crypto.js" sign "$KEYS/priv.pem" "$TAR")"
HASH="$(printf '%s' "$SIGNED" | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>process.stdout.write(JSON.parse(s).hash))')"
SIG="$(printf '%s' "$SIGNED" | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>process.stdout.write(JSON.parse(s).signature))')"
UPDATE_JSON="{\"payloadPath\":\"$TAR\",\"hash\":\"$HASH\",\"signature\":\"$SIG\",\"version\":\"2.0.0\"}"

echo "== run scripted sequence (write → quit → relaunch → read → update → relaunch) =="
DATA="$BUILD/data"; rm -rf "$DATA"; mkdir -p "$DATA"
BIN="$APP/Contents/MacOS/EngawaHost"
RESULT="$DATA/data/autotest-result.json"

launch() { perl -e 'alarm(shift @ARGV); exec @ARGV or die "exec: $!"' 60 "$BIN"; }

echo "-- launch 1: write a record"
ENGAWA_AUTOTEST=1 ENGAWA_DATA_ROOT="$DATA" launch
echo "-- launch 2: relaunch, read it back, stage the signed app-update"
ENGAWA_AUTOTEST=1 ENGAWA_DATA_ROOT="$DATA" ENGAWA_AUTOTEST_UPDATE="$UPDATE_JSON" launch
echo "-- launch 3: relaunch onto the updated slot, confirm, verify the record survived"
ENGAWA_AUTOTEST=1 ENGAWA_DATA_ROOT="$DATA" launch

echo "== result =="
cat "$RESULT"; echo
node -e 'const r=require(process.argv[1]);if(r.final==="PASS"&&r.dataIntact&&r.updated){console.log("make notes: PASS (data survived sqlite; code updated via update)");process.exit(0)}console.log("make notes: FAIL");process.exit(1)' "$RESULT"
