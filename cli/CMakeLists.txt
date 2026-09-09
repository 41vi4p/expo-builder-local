cmake_minimum_required(VERSION 3.16)
# This VERSION is the single source of truth for the CLI's version (see main.cpp's
# --version output) — kept in step with orchestrator/package.json and
# expo-builder-gui/package.json per ../CLAUDE.md's version-management rule.
project(expo-builder-local-cli VERSION 0.24.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(NOT MSVC)
  set(CMAKE_CXX_FLAGS_RELEASE "-O2")
endif()
# MSVC's own default /O2 for a Release config already applies otherwise — no
# equivalent override needed there.

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# The binary/command is named "ebl" (short for expo-local-builder) — deliberately
# distinct from the "expo-builder-local" project/repo name, which stays as the
# directory and shared-data-dir name (see the install() calls below).
#
# http_client_unix.cpp/http_client_win.cpp: platform-specific Docker Engine API
# transport (Unix socket via libcurl vs. Docker Desktop's named pipe, hand-rolled —
# see src/http_client.hpp). winpath.cpp is the identity function on non-Windows, so
# it's built everywhere unconditionally rather than split per-platform.
#
# resources/ebl.rc: Windows-only - embeds resources/ebl.ico (generated from
# ../docs/assets/ebl_logo.png, same source ebl_landing_page/app/favicon.ico uses) as
# ebl.exe's own icon, so it's not just a bare default .exe icon in
# Explorer/taskbar/Add-Remove-Programs. CMake's MSVC generator invokes rc.exe on a
# .rc source automatically, no extra config needed - a no-op on non-Windows, where
# it's never added to the sources list at all.
if(WIN32)
  set(EBL_PLATFORM_SOURCES src/http_client_win.cpp resources/ebl.rc)
else()
  set(EBL_PLATFORM_SOURCES src/http_client_unix.cpp)
endif()

add_executable(ebl
  src/main.cpp
  src/json.cpp
  src/http_client_common.cpp
  ${EBL_PLATFORM_SOURCES}
  src/winpath.cpp
  src/docker_client.cpp
  src/docker_stats.cpp
  src/build_status_view.cpp
  src/tar_writer.cpp
  src/detect.cpp
  src/metrics.cpp
  src/host_info.cpp
  src/version_compare.cpp
  src/update_check.cpp
  src/runner_context.cpp
  src/pull_progress.cpp
  src/base64.cpp
  src/crypto.cpp
  src/config_store.cpp
  src/prompt.cpp
  src/commands/build.cpp
  src/commands/setup.cpp
  src/commands/config.cpp
  src/commands/start.cpp
  src/commands/update.cpp
  src/commands/clean.cpp
  src/commands/completion.cpp
)

target_include_directories(ebl PRIVATE src)
target_compile_definitions(ebl PRIVATE EXPO_BUILDER_CLI_VERSION="${PROJECT_VERSION}")
target_link_libraries(ebl PRIVATE CURL::libcurl OpenSSL::Crypto Threads::Threads)
if(WIN32)
  # http_client_win.cpp/metrics.cpp's CreateProcess-based git capture need these;
  # linked explicitly since vcpkg's curl/openssl targets don't pull them in for us.
  target_link_libraries(ebl PRIVATE ws2_32 advapi32)
  target_compile_options(ebl PRIVATE /W4)
else()
  target_compile_options(ebl PRIVATE -Wall -Wextra)
endif()

# Bundle the Android runner build context (docker/runner/ at the repo root, one level
# up from this cli/ package) into the build directory, so the compiled binary is
# self-contained and can build the runner image itself without the rest of the repo
# being present — see src/runner_context.cpp for the runtime lookup order.
set(RUNNER_SRC_DIR "${CMAKE_SOURCE_DIR}/../docker/runner")
set(RUNNER_BUILD_DIR "${CMAKE_BINARY_DIR}/runner")
if(EXISTS "${RUNNER_SRC_DIR}")
  file(COPY "${RUNNER_SRC_DIR}/" DESTINATION "${RUNNER_BUILD_DIR}")
endif()

install(TARGETS ebl RUNTIME DESTINATION bin)
install(DIRECTORY "${RUNNER_BUILD_DIR}/" DESTINATION share/expo-builder-local/runner OPTIONAL)

# Off by default - a normal `cmake --build` (what a real release/install does) stays
# exactly as fast/minimal as before. CI (.github/workflows/ci.yml's linux-cli-build
# job) turns this on explicitly with -DEBL_BUILD_TESTS=ON; see tests/CMakeLists.txt.
option(EBL_BUILD_TESTS "Build cli/tests/ unit tests (ebl_tests)" OFF)
if(EBL_BUILD_TESTS)
  # enable_testing() has to be called here (top level), not just inside
  # tests/CMakeLists.txt, or `ctest` run from the top of the build dir won't
  # discover the add_test() registered there at all ("No tests were found").
  enable_testing()
  add_subdirectory(tests)
endif()

# --- .deb packaging (`cpack -G DEB` after building — see ../Makefile's `deb` target,
# and ../docs/RELEASING.md for how CI builds + signs this on a tagged release) --------
# Linux/Debian-only — the Windows build ships instead as ebl.exe + an Inno Setup
# installer (see ../windows/).
if(NOT WIN32)
  set(CPACK_PACKAGE_NAME "ebl")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_VENDOR "expo-builder-local")
  set(CPACK_PACKAGE_CONTACT "expo-builder-local maintainers <noreply@example.com>")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
      "Build managed Expo projects into signed Android APK/AABs in a disposable Docker container")
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/41vi4p/expo-builder-local")
  # A .deb is conventionally rooted at /usr, not /usr/local — this only affects the
  # packaged layout, not `make install-cli`'s normal ~/.local install.
  set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

  set(CPACK_GENERATOR "DEB")
  set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
  set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
  set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
  # Let dpkg-shlibdeps determine the actual runtime library Depends (libcurl4 vs.
  # libcurl4t64, libssl3 vs. libssl1.1, etc. vary by Debian/Ubuntu release) rather
  # than hand-pinning package names that would only be correct for one distro
  # version.
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

  include(CPack)
endif()
