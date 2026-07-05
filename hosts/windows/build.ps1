# Build the Engawa Windows reference host (C++ + WebView2) via CMake + Ninja.
#
# Single source of truth for the build: `make host-windows`, the conformance runner, and the
# make-notes gate all produce the same `build/EngawaHost.exe`. Uses the Visual Studio C++ toolchain
# (MSVC + Windows SDK) and the CMake/Ninja that ship inside VS, so nothing extra need be on PATH. The
# WebView2 SDK is restored from NuGet at configure time (see CMakeLists.txt).
$ErrorActionPreference = 'Stop'

$SRC = $PSScriptRoot
$BUILD = Join-Path $SRC 'build'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "vswhere not found; install Visual Studio with the C++ workload" }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "Visual Studio with the C++ (VC Tools) workload not found" }

$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
if (-not (Test-Path $cmake)) { throw "CMake not found in VS; install the 'C++ CMake tools for Windows' component" }

# Bring the MSVC x64 toolchain into this session (sets cl.exe, the Windows SDK, LIB/INCLUDE).
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

& $cmake -S $SRC -B $BUILD -G Ninja -DCMAKE_MAKE_PROGRAM="$ninja" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
& $cmake --build $BUILD
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

Write-Host "built $BUILD\EngawaHost.exe"
