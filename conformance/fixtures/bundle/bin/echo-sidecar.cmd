@echo off
rem Windows image for the declared sidecar `./bin/echo-sidecar` (spec/commands/process.md,
rem "Platform executable resolution"). The Unix fixture is a `#!/usr/bin/env node` script with
rem no extension, which Windows cannot spawn directly; this .cmd runs the same script under node
rem so ONE sidecar source serves both platforms. node is already a conformance dependency.
node "%~dp0echo-sidecar" %*
