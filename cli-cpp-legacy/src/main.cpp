// ebl (expo-local-builder) — standalone CLI for expo-builder-local
//
// Subcommands:
//   ebl setup    one-time: install/verify Docker, pull images
//   ebl config   interactive wizard: projects folder, Expo token, ports
//   ebl start    run the orchestrator + web GUI as Docker containers
//   ebl stop     stop them
//   ebl create   scaffold a brand-new Expo app via `npx create-expo-app`,
//                directly on the host (no Docker) - the starting point before
//                `ebl build` ever comes into play
//   ebl build    build a project into a signed APK/AAB (works standalone — no
//                setup/config/start required at all)
//   ebl update   force-refresh the runner/orchestrator/web images right now,
//                rebuilding the runner from scratch (no cache) if it can't pull
//   ebl clean    remove ebl's own stopped build containers (--all: also cache
//                volumes and pulled images) to reclaim disk space
//   ebl completion   print a shell completion script (bash/zsh/fish/powershell)
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <iostream>
#include <string>

#include "commands/build.hpp"
#include "commands/clean.hpp"
#include "commands/completion.hpp"
#include "commands/config.hpp"
#include "commands/create.hpp"
#include "commands/setup.hpp"
#include "commands/start.hpp"
#include "commands/update.hpp"
#include "color.hpp"
#include "host_info.hpp"
#include "update_check.hpp"

namespace {

#ifdef _WIN32
// color.hpp's escape codes only render as colors if the console opts into VT100
// processing — Windows Terminal already has this on by default, but the legacy
// conhost.exe some users still launch from doesn't. Best-effort: failure here just
// means output falls back to plain text via color.hpp's own isatty() check, not a
// hard error.
void enableAnsiOnWindowsConsole() {
  for (DWORD which : {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
    HANDLE h = ::GetStdHandle(which);
    DWORD mode = 0;
    if (h == INVALID_HANDLE_VALUE || !::GetConsoleMode(h, &mode)) continue;
    ::SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  }
}
#endif

#ifndef EXPO_BUILDER_CLI_VERSION
#define EXPO_BUILDER_CLI_VERSION "0.0.0-dev"
#endif
constexpr const char* kVersion = EXPO_BUILDER_CLI_VERSION;

std::string compilerString() {
#if defined(__clang__)
  return std::string("Clang ") + __clang_version__;
#elif defined(_MSC_VER)
  return "MSVC " + std::to_string(_MSC_VER / 100) + "." + std::to_string(_MSC_VER % 100);
#elif defined(__GNUC__)
  return std::string("GCC ") + __VERSION__;
#else
  return "unknown compiler";
#endif
}

std::string archString() {
#if defined(__aarch64__) || defined(_M_ARM64)
  return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
  return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
  return "x86";
#else
  return "unknown arch";
#endif
}

std::string platformString() {
#ifdef _WIN32
  return "Windows";
#elif defined(__APPLE__)
  return "macOS";
#elif defined(__linux__)
  return "Linux";
#else
  return "Unknown";
#endif
}

void printTopLevelUsage() {
  std::cout << R"(ebl <command> [options]

expo-local-builder — build managed Expo projects into signed Android APK/AABs in a
disposable Docker container, with an optional web GUI.

Commands:
  create    Scaffold a brand-new Expo app (npx create-expo-app, on this machine)
  setup     One-time: check/install Docker, pull images
  config    Interactive wizard: projects folder, Expo token, ports
  start     Run the orchestrator + web GUI (as Docker containers)
  stop      Stop the orchestrator + web GUI
  build     Build a project — works standalone, no setup/config/start required
  update    Force-refresh the runner/orchestrator/web images right now
  clean     Remove stopped build containers (--all: also cache volumes/images)
  completion   Print a shell completion script (bash/zsh/fish/powershell)

Run `ebl <command> --help` for command-specific options. `ebl create . myapp` to
scaffold a new app, or `ebl build .` if you just want a build right now.

  -h, --help      Show this help
  -v, --version   Show version
      --about     Show project/developer/license/repository info
)";
}

void printVersion() {
  std::cout << "ebl " << kVersion
            << " - build managed Expo (SDK 56+) projects into signed Android APK/AABs, "
               "in a disposable Docker container\n\n";
  std::cout << "Built:    " << __DATE__ << " " << __TIME__ << " (" << compilerString() << ")\n";
  std::cout << "Platform: " << platformString() << " " << archString() << "\n";
  std::cout << "Host:     " << ebl::hostOsVersion() << "\n";

  // Rate-limited to at most one real network check per 24h (see update_check.cpp) -
  // `--version` is exactly the moment a user is already asking about versions, so a
  // short (<=2.5s), best-effort check here is expected rather than a surprise
  // background network call on every other command.
  if (auto latest = ebl::checkForNewerVersion(kVersion)) {
    std::cout << "\n"
              << ebl::color::yellow("A newer ebl is available: " + *latest + " (you have " + kVersion + ")") << "\n"
              << ebl::color::dim("  https://github.com/41vi4p/expo-builder-local/releases/latest") << "\n";
  }
}

void printAbout() {
  std::cout << R"(ebl (expo-local-builder) v)"
            << kVersion << R"(

expo-builder-local — scaffold, build, and manage Expo (SDK 56+) projects end to
end: `ebl create` to start a new app, `ebl build` to turn it into a signed
Android APK/AAB entirely on your own machine via a disposable Docker container,
with an optional web GUI. Not affiliated with Expo/Google.

Developer:    41vi4p
License:      GNU General Public License v3.0 (GPL-3.0)
Repository:   https://github.com/41vi4p/expo-builder-local
)";
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  enableAnsiOnWindowsConsole();
#endif
  if (argc == 1) {
    printTopLevelUsage();
    return 0;
  }

  std::string command = argv[1];
  if (command == "-h" || command == "--help") {
    printTopLevelUsage();
    return 0;
  }
  if (command == "-v" || command == "--version") {
    printVersion();
    return 0;
  }
  if (command == "--about" || command == "about") {
    printAbout();
    return 0;
  }

  int subArgc = argc - 2;
  char** subArgv = argv + 2;

  if (command == "create") return ebl::commands::runCreate(subArgc, subArgv);
  if (command == "build") return ebl::commands::runBuild(subArgc, subArgv);
  if (command == "setup") return ebl::commands::runSetup(subArgc, subArgv);
  if (command == "config") return ebl::commands::runConfig(subArgc, subArgv);
  if (command == "start") return ebl::commands::runStart(subArgc, subArgv);
  if (command == "stop") return ebl::commands::runStop(subArgc, subArgv);
  if (command == "update") return ebl::commands::runUpdate(subArgc, subArgv);
  if (command == "clean") return ebl::commands::runClean(subArgc, subArgv);
  if (command == "completion") return ebl::commands::runCompletion(subArgc, subArgv);

  std::cerr << "Unknown command: " << command << "\n\n";
  printTopLevelUsage();
  return 2;
}
