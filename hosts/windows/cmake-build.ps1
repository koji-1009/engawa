# Configure + build a CMake project with the Visual Studio C++ toolchain (MSVC + the CMake/Ninja
# that ship inside VS, so nothing extra need be on PATH). Shared by `build.ps1` (the reference host)
# and the CLI's per-app host generation (engawa dev/build). -Source is the dir with CMakeLists.txt.
param(
  [Parameter(Mandatory)][string]$Source,
  [Parameter(Mandatory)][string]$Build
)
$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "vswhere not found; install Visual Studio with the C++ workload" }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "Visual Studio with the C++ (VC Tools) workload not found" }

$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
if (-not (Test-Path $cmake)) { throw "CMake not found in VS; install the 'C++ CMake tools for Windows' component" }

# Bring the MSVC x64 toolchain into this session (cl.exe, the Windows SDK, LIB/INCLUDE).
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

& $cmake -S $Source -B $Build -G Ninja -DCMAKE_MAKE_PROGRAM="$ninja" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
& $cmake --build $Build
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }
