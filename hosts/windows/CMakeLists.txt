# Engawa Windows reference host — C++ + WebView2 (hosts/windows/README.md).
# Clean-room from spec/ + shell-js/ + conformance/ + adapter spec.md (CLAUDE.md).
#
# The host compiles to a single native EngawaHost.exe that calls WebView2 directly. The end user
# runs it with no runtime prerequisite — the only machine dependency is the WebView2 Evergreen
# runtime, a system component (docs/design.md). Build-time cost (MSVC, Windows SDK, WebView2 SDK
# headers, and the fetched third-party sources) is the developer's; the shipped artifact is a plain
# native binary.
#
# Console subsystem (WIN32 executable is NOT set): the conformance runner drives the host over a
# stdin/stdout control channel (conformance/hosts/windows/driver.js), so the standard streams must
# stay attached.

cmake_minimum_required(VERSION 3.20)

# CMP0091: select the MSVC runtime via CMAKE_MSVC_RUNTIME_LIBRARY (below), so we can link the CRT
# statically (/MT) — no vcredist on the user's machine, a self-contained native exe.
cmake_policy(SET CMP0091 NEW)

project(EngawaHost LANGUAGES C CXX)  # C for the fetched sqlite3.c / tweetnacl.c

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --- WebView2 SDK (C++ headers + static loader) ------------------------------------------------
# The WebView2 C++ API is the first-party COM surface; the loader is linked statically so no
# WebView2Loader.dll ships beside the exe. The SDK is a NuGet package; resolve it from the global
# packages cache (populated by `nuget restore` / prior `dotnet restore`), overridable for CI.
set(WEBVIEW2_VERSION "1.0.4022.49" CACHE STRING "Microsoft.Web.WebView2 SDK version")
set(WEBVIEW2_SDK_DIR "" CACHE PATH "Path to the Microsoft.Web.WebView2 package root (build/native/... beneath)")

if(NOT WEBVIEW2_SDK_DIR)
  set(_nuget_roots "$ENV{NUGET_PACKAGES}" "$ENV{USERPROFILE}/.nuget/packages" "$ENV{HOME}/.nuget/packages")
  foreach(_root ${_nuget_roots})
    if(_root AND EXISTS "${_root}/microsoft.web.webview2/${WEBVIEW2_VERSION}/build/native/include/WebView2.h")
      set(WEBVIEW2_SDK_DIR "${_root}/microsoft.web.webview2/${WEBVIEW2_VERSION}")
      break()
    endif()
  endforeach()
endif()

if(NOT WEBVIEW2_SDK_DIR OR NOT EXISTS "${WEBVIEW2_SDK_DIR}/build/native/include/WebView2.h")
  message(FATAL_ERROR
    "WebView2 SDK ${WEBVIEW2_VERSION} not found. Restore it (nuget install Microsoft.Web.WebView2 "
    "-Version ${WEBVIEW2_VERSION}) or pass -DWEBVIEW2_SDK_DIR=<package root>.")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_wv2_arch "x64")
else()
  set(_wv2_arch "x86")
endif()
set(WEBVIEW2_INCLUDE "${WEBVIEW2_SDK_DIR}/build/native/include")
set(WEBVIEW2_LOADER "${WEBVIEW2_SDK_DIR}/build/native/${_wv2_arch}/WebView2LoaderStatic.lib")

message(STATUS "WebView2 SDK:   ${WEBVIEW2_SDK_DIR}")

# --- Third-party sources, fetched at configure time (pinned version + SHA-256) ------------------
# Not vendored into the repo: each is downloaded once into the (git-ignored) build tree and verified
# by hash, so the build is deterministic without carrying ~11 MB of third-party code in git history.
# json.hpp: wire protocol / arg parsing (§2). sqlite3: the reference sqlite adapter's engine.
# tweetnacl: ed25519 verification for the update trust root (§7.1). SHA-256 comes from Windows CNG
# (bcrypt). All compiled/linked in — nothing rides beside the shipped binary. See THIRD_PARTY.md.
set(DEPS "${CMAKE_BINARY_DIR}/_deps")

set(JSON_HPP "${DEPS}/nlohmann/json.hpp")
if(NOT EXISTS "${JSON_HPP}")
  message(STATUS "fetching nlohmann/json 3.11.3")
  file(DOWNLOAD
    "https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp"
    "${JSON_HPP}" TLS_VERIFY ON
    EXPECTED_HASH SHA256=9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6)
endif()

set(TWEETNACL_C "${DEPS}/tweetnacl/tweetnacl.c")
set(TWEETNACL_H "${DEPS}/tweetnacl/tweetnacl.h")
if(NOT EXISTS "${TWEETNACL_C}")
  message(STATUS "fetching tweetnacl 20140427")
  file(DOWNLOAD "https://tweetnacl.cr.yp.to/20140427/tweetnacl.c" "${TWEETNACL_C}" TLS_VERIFY ON
    EXPECTED_HASH SHA256=02e65bc3013ff2168983365e55906bc783c4c7e0a60d8100f17bb303a17175c4)
endif()
if(NOT EXISTS "${TWEETNACL_H}")
  file(DOWNLOAD "https://tweetnacl.cr.yp.to/20140427/tweetnacl.h" "${TWEETNACL_H}" TLS_VERIFY ON
    EXPECTED_HASH SHA256=43f29ad721d9927b747b0100ab4160c119e7bb180c7c98a66e4bf79d31244287)
endif()

# SQLite amalgamation version in ONE place: the release year-path and the numbered archive/dir name
# (bumping sqlite = edit these two + the hash below, nothing else).
set(SQLITE_YEAR 2024)
set(SQLITE_AMALGAMATION "sqlite-amalgamation-3460100")
set(SQLITE_DIR "${DEPS}/sqlite")
set(SQLITE_C "${SQLITE_DIR}/sqlite3.c")
if(NOT EXISTS "${SQLITE_C}")
  message(STATUS "fetching ${SQLITE_AMALGAMATION}")
  set(_sqlite_zip "${DEPS}/sqlite.zip")
  file(DOWNLOAD "https://www.sqlite.org/${SQLITE_YEAR}/${SQLITE_AMALGAMATION}.zip" "${_sqlite_zip}" TLS_VERIFY ON
    EXPECTED_HASH SHA256=77823cb110929c2bcb0f5d48e4833b5c59a8a6e40cdea3936b99e199dbbe5784)
  file(ARCHIVE_EXTRACT INPUT "${_sqlite_zip}" DESTINATION "${DEPS}/sqlite-x")
  file(COPY "${DEPS}/sqlite-x/${SQLITE_AMALGAMATION}/sqlite3.c"
            "${DEPS}/sqlite-x/${SQLITE_AMALGAMATION}/sqlite3.h"
       DESTINATION "${SQLITE_DIR}")
endif()

# --- Sources -----------------------------------------------------------------------------------
# The built-in namespaces (§4) and the sqlite/update adapters compile into this one exe: Engawa's
# composition is static at app build time (§3, "no dynamic loading").
file(GLOB_RECURSE HOST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE ADAPTER_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/../../adapters/sqlite/hosts/windows/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../adapters/update/hosts/windows/*.cpp")

add_executable(EngawaHost ${HOST_SOURCES} ${ADAPTER_SOURCES} "${SQLITE_C}" "${TWEETNACL_C}")

target_include_directories(EngawaHost PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${DEPS}"             # so <nlohmann/json.hpp> resolves
  "${SQLITE_DIR}"       # sqlite3.h
  "${DEPS}/tweetnacl"   # tweetnacl.h
  "${WEBVIEW2_INCLUDE}")

target_compile_definitions(EngawaHost PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
  SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0)

# UTF-8 source/exec charset (json.hpp + our string literals are UTF-8); quiet the fetched C sources'
# benign MSVC warnings so a clean build stays readable, without suppressing warnings on our own code.
target_compile_options(EngawaHost PRIVATE /utf-8)
set_source_files_properties("${SQLITE_C}" "${TWEETNACL_C}" PROPERTIES COMPILE_OPTIONS "/w")

# WRL (COM callbacks) + WebView2 static loader + CNG (bcrypt, SHA-256) + system libs. No external
# package: WRL ships in the Windows SDK, everything else is linked in, so the exe stands alone.
target_link_libraries(EngawaHost PRIVATE
  "${WEBVIEW2_LOADER}"
  bcrypt ole32 oleaut32 shlwapi version shell32 user32 gdi32 advapi32
  runtimeobject)  # WinRT activation (notification toasts via Windows.UI.Notifications)
