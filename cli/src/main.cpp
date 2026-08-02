// ebl (expo-local-builder) — standalone CLI for expo-builder-local
//
// Subcommands:
//   ebl setup    one-time: install/verify Docker, pull images
//   ebl config   interactive wizard: projects folder, Expo token, ports
//   ebl start    run the orchestrator + web GUI as Docker containers
//   ebl stop     stop them
//   ebl build    build a project into a signed APK/AAB (works standalone — no
//                setup/config/start required at all)
//   ebl clean    remove ebl's own stopped build containers (--all: also cache
//                volumes and pulled images) to reclaim disk space
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <iostream>
#include <string>

#include "commands/build.hpp"
#include "commands/clean.hpp"
#include "commands/config.hpp"
#include "commands/setup.hpp"
#include "commands/start.hpp"

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

void printTopLevelUsage() {
  std::cout << R"(ebl <command> [options]

expo-local-builder — build managed Expo projects into signed Android APK/AABs in a
disposable Docker container, with an optional web GUI.

Commands:
  setup     One-time: check/install Docker, pull images
  config    Interactive wizard: projects folder, Expo token, ports
  start     Run the orchestrator + web GUI (as Docker containers)
  stop      Stop the orchestrator + web GUI
  build     Build a project — works standalone, no setup/config/start required
  clean     Remove stopped build containers (--all: also cache volumes/images)

Run `ebl <command> --help` for command-specific options. `ebl build .` is the most
common starting point if you just want a build right now.

  -h, --help      Show this help
  -v, --version   Show version
      --about     Show project/developer/license/repository info
)";
}

void printAbout() {
  std::cout << R"(ebl (expo-local-builder) v)"
            << kVersion << R"(

expo-builder-local — build managed Expo (SDK 56+) projects into signed Android
APK/AABs entirely on your own machine, via a disposable Docker container, with
an optional web GUI. Not affiliated with Expo/Google.

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
    std::cout << "ebl " << kVersion << "\n";
    return 0;
  }
  if (command == "--about" || command == "about") {
    printAbout();
    return 0;
  }

  int subArgc = argc - 2;
  char** subArgv = argv + 2;

  if (command == "build") return ebl::commands::runBuild(subArgc, subArgv);
  if (command == "setup") return ebl::commands::runSetup(subArgc, subArgv);
  if (command == "config") return ebl::commands::runConfig(subArgc, subArgv);
  if (command == "start") return ebl::commands::runStart(subArgc, subArgv);
  if (command == "stop") return ebl::commands::runStop(subArgc, subArgv);
  if (command == "clean") return ebl::commands::runClean(subArgc, subArgv);

  std::cerr << "Unknown command: " << command << "\n\n";
  printTopLevelUsage();
  return 2;
}
