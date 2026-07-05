# The make-notes acceptance gate, Windows edition — the PowerShell twin of gate.sh. Proves the twin
# claims in one run: data survives (sqlite) and code updates (update). No UI scripting — examples/notes
# drives itself in autotest mode, one phase per launch, reporting via exit code + a result file.
$ErrorActionPreference = 'Stop'

$REPO  = (Resolve-Path "$PSScriptRoot/../../..").Path
$BUILD = Join-Path $REPO 'build/notes'
$GATE  = Join-Path $REPO 'examples/notes/gate'
Remove-Item -Recurse -Force $BUILD -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $BUILD | Out-Null

Write-Host "== build host (with sqlite + update adapters) =="
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $REPO 'hosts/windows/build.ps1')
if ($LASTEXITCODE -ne 0) { throw "host build failed" }
$HOSTEXE = Join-Path $REPO 'hosts/windows/build/EngawaHost.exe'

Write-Host "== dev signing keypair (section 7.1 trust root) =="
$KEYS = Join-Path $BUILD 'keys'; New-Item -ItemType Directory -Force $KEYS | Out-Null
node "$GATE/crypto.js" keygen $KEYS | Out-Null
$TRUST = (Get-Content (Join-Path $KEYS 'trust-root.txt') -Raw).Trim()

Write-Host "== bundle app =="
$APP = Join-Path $BUILD 'app'; New-Item -ItemType Directory -Force $APP | Out-Null
Copy-Item -Recurse -Force (Join-Path $REPO 'examples/notes/app/*') $APP
$BUNDLE  = Join-Path $REPO 'examples/notes'          # engawa.json: version + sidecars
$SHELLJS = Join-Path $REPO 'shell-js/shell.js'

Write-Host "== build + sign app-update payload (version 2) =="
$UPDSRC = Join-Path $BUILD 'update-src'; New-Item -ItemType Directory -Force $UPDSRC | Out-Null
Copy-Item -Recurse -Force (Join-Path $REPO 'examples/notes/app/*') $UPDSRC
# Bump the asset version 1 → 2 for the update payload. Done in PowerShell (not `node -e`) so no inline
# double-quotes cross the shell boundary — Windows PowerShell 5.1 strips them from native-command args.
$IDXV2 = Join-Path $UPDSRC 'index.html'
(Get-Content $IDXV2 -Raw).Replace('content="1"', 'content="2"') | Set-Content -NoNewline $IDXV2
$TAR = Join-Path $BUILD 'app-v2.tar'
# Use the system bsdtar explicitly: a Git-for-Windows GNU tar on PATH misreads the drive-letter path
# ("C:" read as a remote host) — the same reason the conformance suite pins System32\tar.exe.
& "$env:SystemRoot\System32\tar.exe" -cf $TAR -C $UPDSRC .
if ($LASTEXITCODE -ne 0) { throw "tar failed to build the update payload" }
$SIGNED = node "$GATE/crypto.js" sign (Join-Path $KEYS 'priv.pem') $TAR | ConvertFrom-Json
$UPDATE_JSON = @{ payloadPath = $TAR; hash = $SIGNED.hash; signature = $SIGNED.signature; version = '2.0.0' } | ConvertTo-Json -Compress

Write-Host "== run scripted sequence (write -> quit -> relaunch -> read -> update -> relaunch) =="
$DATA = Join-Path $BUILD 'data'
Remove-Item -Recurse -Force $DATA -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $DATA | Out-Null
$RESULT = Join-Path $DATA 'data/autotest-result.json'

function Invoke-Launch($updateJson) {
    $env:ENGAWA_AUTOTEST   = '1'
    $env:ENGAWA_DATA_ROOT  = $DATA
    $env:ENGAWA_APP_ROOT   = $APP
    $env:ENGAWA_SHELL_JS   = $SHELLJS
    $env:ENGAWA_BUNDLE_ROOT = $BUNDLE
    $env:ENGAWA_TRUST_ROOT = $TRUST
    if ($updateJson) { $env:ENGAWA_AUTOTEST_UPDATE = $updateJson }
    else { Remove-Item Env:ENGAWA_AUTOTEST_UPDATE -ErrorAction SilentlyContinue }

    $p = Start-Process -FilePath $HOSTEXE -PassThru -NoNewWindow
    if (-not $p.WaitForExit(60000)) { $p.Kill(); throw "launch timed out" }
}

Write-Host "-- launch 1: write a record"
Invoke-Launch $null
Write-Host "-- launch 2: relaunch, read it back, stage the signed app-update"
Invoke-Launch $UPDATE_JSON
Write-Host "-- launch 3: relaunch onto the updated slot, confirm, verify the record survived"
Invoke-Launch $null

Write-Host "== result =="
Get-Content $RESULT
# Evaluate the pass criteria in PowerShell (no `node -e` inline quotes; see the v2 rewrite above).
$r = Get-Content $RESULT -Raw | ConvertFrom-Json
if ($r.final -eq 'PASS' -and $r.dataIntact -and $r.updated) {
    Write-Host "make notes: PASS (data survived sqlite; code updated via update)"
    exit 0
}
Write-Host "make notes: FAIL"
exit 1
