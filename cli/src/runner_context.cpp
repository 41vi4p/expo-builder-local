#include "runner_context.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace ebl {

namespace {

fs::path selfExecutablePath() {
#ifdef _WIN32
  std::vector<char> buf(MAX_PATH);
  DWORD len = ::GetModuleFileNameA(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
  if (len == 0 || len >= buf.size()) {
    throw std::runtime_error("Could not resolve the running executable's own path (GetModuleFileNameA)");
  }
  return fs::path(std::string(buf.data(), len));
#else
  std::vector<char> buf(4096);
  ssize_t len = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
  if (len <= 0) throw std::runtime_error("Could not resolve the running executable's own path (/proc/self/exe)");
  buf[static_cast<size_t>(len)] = '\0';
  return fs::path(buf.data());
#endif
}

bool looksLikeRunnerContext(const fs::path& dir) {
  return fs::exists(dir / "Dockerfile");
}

}  // namespace

std::string resolveRunnerContextDir() {
  if (const char* envDir = std::getenv("EXPO_BUILDER_RUNNER_DIR")) {
    if (looksLikeRunnerContext(fs::path(envDir))) return envDir;
  }

  fs::path exeDir = selfExecutablePath().parent_path();

  // 1) `cmake --install` layout: <prefix>/bin/expo-builder-local -> <prefix>/share/expo-builder-local/runner
  fs::path installed = exeDir.parent_path() / "share" / "expo-builder-local" / "runner";
  if (looksLikeRunnerContext(installed)) return installed.string();

  // 2) Running straight from the CMake build directory: <build>/expo-builder-local -> <build>/runner
  fs::path buildDirCopy = exeDir / "runner";
  if (looksLikeRunnerContext(buildDirCopy)) return buildDirCopy.string();

  throw std::runtime_error(
      "Could not locate the bundled Android runner build context (looked in " + installed.string() + " and " +
      buildDirCopy.string() +
      "). Set EXPO_BUILDER_RUNNER_DIR to docker/runner from an expo-builder-local checkout, or rebuild this CLI.");
}

}  // namespace ebl
