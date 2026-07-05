# engawa-host.cmake — reusable Windows host composition (docs/design.md "static composition").
#
# Including this file resolves the WebView2 SDK and fetches the core third-party deps (nlohmann/json,
# tweetnacl) once, then defines:
#
#   engawa_add_host(<target>
#     ADAPTERS <dir>...   # each an adapters/<name>/hosts/windows dir: its *.cpp are compiled in, and
#                         #   an optional deps.cmake declares the adapter's native deps (see below)
#     COMPOSE  <file>)    # a .cpp defining registerAppAdapters() — registers exactly those adapters
#
# It builds a single native EngawaHost.exe from the host core (built-ins §4 + infra) + the
# contract-coupled `update` adapter (§7.1/§8, always) + the given app adapters + the compose TU.
#
# Adapter native-dependency contract (spec/commands/README.md): an adapter dir may contain a
# `deps.cmake`. It runs with `DEPS` (a git-ignored fetch dir) available and may append native sources
# to `ENGAWA_EXTRA_SOURCES` and include dirs to `ENGAWA_EXTRA_INCLUDES` (e.g. the sqlite adapter fetches
# the SQLite amalgamation). The full reference host (hosts/windows/CMakeLists.txt) composes the
# in-tree `sqlite`; `engawa dev/build` generates a CMakeLists that composes exactly the app's adapters.

set(ENGAWA_WINDOWS_DIR "${CMAKE_CURRENT_LIST_DIR}")
get_filename_component(ENGAWA_REPO "${ENGAWA_WINDOWS_DIR}/../.." ABSOLUTE)
set(DEPS "${CMAKE_BINARY_DIR}/_deps")

# --- WebView2 SDK (C++ headers + static loader) ------------------------------------------------
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

# --- Core third-party (fetched, pinned + SHA-256): json (wire, §2), tweetnacl (update trust, §7.1) --
set(JSON_HPP "${DEPS}/nlohmann/json.hpp")
if(NOT EXISTS "${JSON_HPP}")
  message(STATUS "fetching nlohmann/json 3.11.3")
  file(DOWNLOAD "https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp"
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

# --- engawa_add_host -------------------------------------------------------------------------
function(engawa_add_host TARGET)
  cmake_parse_arguments(H "" "COMPOSE" "ADAPTERS" ${ARGN})

  # Host core: everything under src/ EXCEPT Compose.cpp (the compose TU is provided per build).
  file(GLOB_RECURSE _core CONFIGURE_DEPENDS "${ENGAWA_WINDOWS_DIR}/src/*.cpp")
  list(FILTER _core EXCLUDE REGEX "/Compose\\.cpp$")
  # `update` is contract-coupled — always composed in (docs/design.md "Composition").
  file(GLOB _update CONFIGURE_DEPENDS "${ENGAWA_REPO}/adapters/update/hosts/windows/*.cpp")

  set(ENGAWA_EXTRA_SOURCES "")
  set(ENGAWA_EXTRA_INCLUDES "")
  set(_adapter_sources "")
  foreach(_adir ${H_ADAPTERS})
    file(GLOB _asrc CONFIGURE_DEPENDS "${_adir}/*.cpp")
    list(APPEND _adapter_sources ${_asrc})
    if(EXISTS "${_adir}/deps.cmake")
      include("${_adir}/deps.cmake")  # may append to ENGAWA_EXTRA_SOURCES / _INCLUDES
    endif()
  endforeach()

  add_executable(${TARGET}
    ${_core} ${_update} ${_adapter_sources} "${H_COMPOSE}"
    "${TWEETNACL_C}" ${ENGAWA_EXTRA_SOURCES})

  target_include_directories(${TARGET} PRIVATE
    "${ENGAWA_WINDOWS_DIR}/src"
    "${DEPS}"            # <nlohmann/json.hpp>
    "${DEPS}/tweetnacl"  # tweetnacl.h
    "${WEBVIEW2_INCLUDE}"
    ${ENGAWA_EXTRA_INCLUDES})

  target_compile_definitions(${TARGET} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(${TARGET} PRIVATE /utf-8)
  set_source_files_properties("${TWEETNACL_C}" PROPERTIES COMPILE_OPTIONS "/w")

  target_link_libraries(${TARGET} PRIVATE
    "${WEBVIEW2_LOADER}"
    bcrypt ole32 oleaut32 shlwapi version shell32 user32 gdi32 advapi32
    runtimeobject)  # WinRT activation (notification toasts)
endfunction()
