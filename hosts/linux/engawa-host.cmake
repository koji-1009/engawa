# engawa-host.cmake — reusable Linux host composition (docs/design.md "static composition").
#
# Including this file resolves the GTK3 / WebKitGTK / libsoup / libsodium system packages (via
# pkg-config) and fetches nlohmann/json (wire, §2) once, then defines:
#
#   engawa_add_host(<target>
#     ADAPTERS <dir>...   # each an adapters/<name>/hosts/linux dir: its *.cpp are compiled in, and an
#                         #   optional deps.cmake declares the adapter's native deps (see below)
#     COMPOSE  <file>)    # a .cpp defining registerAppAdapters() — registers exactly those adapters
#
# It builds a single native EngawaHost from the host core (built-ins §4 + infra) + the contract-coupled
# `update` adapter (§7.1/§8, always) + the given app adapters + the compose TU. Crypto (§7.1 SHA-256 +
# ed25519) is libsodium, so — unlike the Windows host — no tweetnacl is fetched.
#
# Adapter native-dependency contract (docs/design.md "Composition"): an adapter dir may contain a
# `deps.cmake`. It runs with `DEPS` (a git-ignored fetch dir) available and may append native sources
# to `ENGAWA_EXTRA_SOURCES` and include dirs to `ENGAWA_EXTRA_INCLUDES` (e.g. the sqlite adapter fetches
# the SQLite amalgamation).

set(ENGAWA_LINUX_DIR "${CMAKE_CURRENT_LIST_DIR}")
get_filename_component(ENGAWA_REPO "${ENGAWA_LINUX_DIR}/../.." ABSOLUTE)
set(DEPS "${CMAKE_BINARY_DIR}/_deps")

# --- system packages (GTK3 + WebKitGTK + libsoup3 + libsodium) ---------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK REQUIRED gtk+-3.0)
pkg_check_modules(WEBKIT REQUIRED webkit2gtk-4.1)
pkg_check_modules(SOUP REQUIRED libsoup-3.0)
pkg_check_modules(SODIUM REQUIRED libsodium)

# --- nlohmann/json (fetched, pinned + SHA-256): the wire codec (§2) ----------------------------
set(JSON_HPP "${DEPS}/nlohmann/json.hpp")
if(NOT EXISTS "${JSON_HPP}")
  message(STATUS "fetching nlohmann/json 3.11.3")
  file(DOWNLOAD "https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp"
    "${JSON_HPP}" TLS_VERIFY ON
    EXPECTED_HASH SHA256=9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6)
endif()

# --- engawa_add_host ---------------------------------------------------------------------------
function(engawa_add_host TARGET)
  cmake_parse_arguments(H "" "COMPOSE" "ADAPTERS" ${ARGN})

  # Host core: everything under src/ EXCEPT Compose.cpp (the compose TU is provided per build).
  file(GLOB_RECURSE _core CONFIGURE_DEPENDS "${ENGAWA_LINUX_DIR}/src/*.cpp")
  list(FILTER _core EXCLUDE REGEX "/Compose\\.cpp$")
  # `update` is contract-coupled — always composed in (docs/design.md "Composition").
  file(GLOB _update CONFIGURE_DEPENDS "${ENGAWA_REPO}/adapters/update/hosts/linux/*.cpp")

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
    ${_core} ${_update} ${_adapter_sources} "${H_COMPOSE}" ${ENGAWA_EXTRA_SOURCES})

  target_include_directories(${TARGET} PRIVATE
    "${ENGAWA_LINUX_DIR}/src"
    "${DEPS}"  # <nlohmann/json.hpp>
    ${GTK_INCLUDE_DIRS} ${WEBKIT_INCLUDE_DIRS} ${SOUP_INCLUDE_DIRS} ${SODIUM_INCLUDE_DIRS}
    ${ENGAWA_EXTRA_INCLUDES})

  # -Wall/-Wextra on our sources; -Wno-deprecated-declarations because WebKitGTK's per-instance
  # website-data-manager context ctor is deprecated in 2.4x but is the 4.1 API we target.
  target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Wno-deprecated-declarations
    ${GTK_CFLAGS_OTHER} ${WEBKIT_CFLAGS_OTHER} ${SOUP_CFLAGS_OTHER})

  target_link_directories(${TARGET} PRIVATE
    ${GTK_LIBRARY_DIRS} ${WEBKIT_LIBRARY_DIRS} ${SOUP_LIBRARY_DIRS} ${SODIUM_LIBRARY_DIRS})
  target_link_libraries(${TARGET} PRIVATE
    ${GTK_LIBRARIES} ${WEBKIT_LIBRARIES} ${SOUP_LIBRARIES} ${SODIUM_LIBRARIES} pthread)
endfunction()
