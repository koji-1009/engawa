# Build the Engawa Windows reference host (the FULL host: built-ins + update + reference sqlite) that
# `make host-windows`, the conformance runner, and the make-notes gate use. `engawa dev/build`
# generates its own per-app CMakeLists and calls cmake-build.ps1 directly. Output: build/EngawaHost.exe.
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'cmake-build.ps1') -Source $PSScriptRoot -Build (Join-Path $PSScriptRoot 'build')
Write-Host "built $(Join-Path $PSScriptRoot 'build\EngawaHost.exe')"
