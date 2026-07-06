# The Linux reference host (C++ + GTK3 + WebKitGTK). Thin: the reusable composition lives in
# engawa-host.cmake; here we compose the in-tree reference `sqlite` adapter + the default Compose.cpp
# (which registers sqlite and provides an empty baked trust root — the env root is used under
# conformance / make-notes). `engawa dev/build` generates its own CMakeLists that composes exactly the
# app's declared adapters instead.
cmake_minimum_required(VERSION 3.20)
project(EngawaHost LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

include(engawa-host.cmake)

engawa_add_host(EngawaHost
  ADAPTERS "${ENGAWA_REPO}/adapters/sqlite/hosts/linux"
  COMPOSE  "${ENGAWA_LINUX_DIR}/src/Compose.cpp")
