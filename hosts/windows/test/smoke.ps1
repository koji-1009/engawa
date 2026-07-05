# Build + run the native-path smoke test (hosts/windows/test/native_smoke.cpp) against the host's own
# NativeToast.cpp, using the VS C++ toolchain. Invoked by `make host-windows-smoke`. Exercises the
# real toast + file-dialog COM paths that the conformance suite cannot (it uses substitutes); the
# interactive file-pick result stays out of scope. Exit 0 = SMOKE OK.
$ErrorActionPreference = 'Stop'

$TEST = $PSScriptRoot
$SRC = Join-Path (Split-Path $TEST -Parent) 'src'
$OUT = Join-Path (Split-Path $TEST -Parent) 'build\smoke'
New-Item -ItemType Directory -Force $OUT | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "Visual Studio with the C++ (VC Tools) workload not found" }
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

$exe = Join-Path $OUT 'native_smoke.exe'
& cl /nologo /std:c++20 /EHsc /utf-8 /DUNICODE /D_UNICODE "/I$SRC" `
    (Join-Path $TEST 'native_smoke.cpp') (Join-Path $SRC 'NativeToast.cpp') `
    "/Fo:$OUT\" "/Fe:$exe" `
    ole32.lib oleaut32.lib shell32.lib advapi32.lib runtimeobject.lib | Out-Host
if ($LASTEXITCODE -ne 0) { throw "smoke compile failed" }

& $exe
$code = $LASTEXITCODE
Write-Host "exit=$code"
exit $code
