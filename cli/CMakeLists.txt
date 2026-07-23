cmake_minimum_required(VERSION 3.16)
# This VERSION is the single source of truth for the CLI's version (see main.cpp's
# --version output) — kept in step with orchestrator/package.json and
# expo-builder-gui/package.json per ../CLAUDE.md's version-management rule.
project(expo-builder-local-cli VERSION 0.4.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# The binary/command is named "ebl" (short for expo-local-builder) — deliberately
# distinct from the "expo-builder-local" project/repo name, which stays as the
# directory and shared-data-dir name (see the install() calls below).
add_executable(ebl
  src/main.cpp
  src/json.cpp
  src/http_client.cpp
  src/docker_client.cpp
  src/tar_writer.cpp
  src/detect.cpp
  src/metrics.cpp
  src/runner_context.cpp
  src/base64.cpp
  src/crypto.cpp
  src/config_store.cpp
  src/commands/build.cpp
  src/commands/setup.cpp
  src/commands/config.cpp
  src/commands/start.cpp
)

target_include_directories(ebl PRIVATE src)
target_compile_definitions(ebl PRIVATE EXPO_BUILDER_CLI_VERSION="${PROJECT_VERSION}")
target_link_libraries(ebl PRIVATE CURL::libcurl OpenSSL::Crypto Threads::Threads)
target_compile_options(ebl PRIVATE -Wall -Wextra)

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

# --- .deb packaging (`cpack -G DEB` after building — see ../Makefile's `deb` target,
# and ../docs/RELEASING.md for how CI builds + signs this on a tagged release) --------
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
# libcurl4t64, libssl3 vs. libssl1.1, etc. vary by Debian/Ubuntu release) rather than
# hand-pinning package names that would only be correct for one distro version.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

include(CPack)
