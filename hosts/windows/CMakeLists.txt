# Engawa Windows reference host — C++ + WebView2 (hosts/windows/README.md).
# Clean-room from spec/ + shell-js/ + conformance/ + adapter spec.md (CLAUDE.md).
#
# This is the FULL reference host the conformance suite and the make-notes gate build: the built-ins
# (§4) + the contract-coupled `update` + the reference `sqlite` adapter, so every namespace the suite
# exercises is present. `engawa dev/build` generates its own CMakeLists via the same engawa_add_host
# to compose exactly the adapters an app declares (docs/design.md "static composition").
#
# The host compiles to a single native EngawaHost.exe that calls WebView2 directly — no runtime
# prerequisite for the end user beyond the WebView2 Evergreen system component. Console subsystem
# (WIN32 not set): the conformance runner drives it over a stdin/stdout control channel.

cmake_minimum_required(VERSION 3.20)

# CMP0091: select the MSVC runtime via CMAKE_MSVC_RUNTIME_LIBRARY (below), so the CRT links statically
# (/MT) — no vcredist on the user's machine, a self-contained native exe.
cmake_policy(SET CMP0091 NEW)

project(EngawaHost LANGUAGES C CXX)  # C for the fetched sqlite3.c / tweetnacl.c

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include("${CMAKE_CURRENT_LIST_DIR}/engawa-host.cmake")

engawa_add_host(EngawaHost
  ADAPTERS "${CMAKE_CURRENT_LIST_DIR}/../../adapters/sqlite/hosts/windows"
  COMPOSE  "${CMAKE_CURRENT_LIST_DIR}/src/Compose.cpp")
