# The sqlite adapter's native dependency (Linux): the SQLite amalgamation, fetched pinned + hashed
# and compiled into whatever host composes this adapter. Consumed by engawa_add_host (hosts/linux/
# engawa-host.cmake) — `DEPS` is the git-ignored fetch dir; append to ENGAWA_EXTRA_SOURCES/_INCLUDES.
# Same pinned amalgamation as the Windows adapter (behavioural parity); only the compiler flag differs.
set(_sqlite_year 2024)
set(_sqlite_amalg "sqlite-amalgamation-3460100")
set(_sqlite_dir "${DEPS}/sqlite")
set(_sqlite_c "${_sqlite_dir}/sqlite3.c")
if(NOT EXISTS "${_sqlite_c}")
  message(STATUS "fetching ${_sqlite_amalg}")
  set(_sqlite_zip "${DEPS}/sqlite.zip")
  file(DOWNLOAD "https://www.sqlite.org/${_sqlite_year}/${_sqlite_amalg}.zip" "${_sqlite_zip}" TLS_VERIFY ON
    EXPECTED_HASH SHA256=77823cb110929c2bcb0f5d48e4833b5c59a8a6e40cdea3936b99e199dbbe5784)
  file(ARCHIVE_EXTRACT INPUT "${_sqlite_zip}" DESTINATION "${DEPS}/sqlite-x")
  file(COPY "${DEPS}/sqlite-x/${_sqlite_amalg}/sqlite3.c" "${DEPS}/sqlite-x/${_sqlite_amalg}/sqlite3.h"
       DESTINATION "${_sqlite_dir}")
endif()

list(APPEND ENGAWA_EXTRA_SOURCES "${_sqlite_c}")
list(APPEND ENGAWA_EXTRA_INCLUDES "${_sqlite_dir}")
set_source_files_properties("${_sqlite_c}" PROPERTIES
  COMPILE_OPTIONS "-w"
  COMPILE_DEFINITIONS "SQLITE_THREADSAFE=1;SQLITE_OMIT_LOAD_EXTENSION;SQLITE_DQS=0")
