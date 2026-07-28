cmake_minimum_required(VERSION 3.16)
# This VERSION tracks the CLI's own version (cli/CMakeLists.txt) — the launcher
# only makes sense paired with a specific `ebl` build inside WSL, so it ships
# alongside it rather than versioning independently. See ../../CLAUDE.md.
project(ebl-windows-launcher VERSION 0.8.2 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR
    "windows/launcher is Windows-only: it wraps wsl.exe, which only exists on "
    "Windows. Build cli/ instead for Linux/WSL itself.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# No curl/OpenSSL/Docker API here at all — this binary only ever spawns wsl.exe
# and forwards stdio; all Docker/HTTP logic stays in the real `ebl` running
# inside WSL (cli/), completely unmodified.
add_executable(ebl src/main.cpp)

install(TARGETS ebl RUNTIME DESTINATION .)
