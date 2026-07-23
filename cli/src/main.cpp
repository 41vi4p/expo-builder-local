// ebl (expo-local-builder) — standalone CLI for expo-builder-local
//
// Subcommands:
//   ebl setup    one-time: install/verify Docker, pull images
//   ebl config   interactive wizard: projects folder, Expo token, ports
//   ebl start    run the orchestrator + web GUI as Docker containers
//   ebl stop     stop them
//   ebl build    build a project into a signed APK/AAB (works standalone — no
//                setup/config/start required at all)
#include <iostream>
#include <string>

#include "commands/build.hpp"
#include "commands/config.hpp"
#include "commands/setup.hpp"
#include "commands/start.hpp"

namespace {

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

Run `ebl <command> --help` for command-specific options. `ebl build .` is the most
common starting point if you just want a build right now.

  -h, --help      Show this help
  -v, --version   Show version
)";
}

}  // namespace

int main(int argc, char** argv) {
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

  int subArgc = argc - 2;
  char** subArgv = argv + 2;

  if (command == "build") return ebl::commands::runBuild(subArgc, subArgv);
  if (command == "setup") return ebl::commands::runSetup(subArgc, subArgv);
  if (command == "config") return ebl::commands::runConfig(subArgc, subArgv);
  if (command == "start") return ebl::commands::runStart(subArgc, subArgv);
  if (command == "stop") return ebl::commands::runStop(subArgc, subArgv);

  std::cerr << "Unknown command: " << command << "\n\n";
  printTopLevelUsage();
  return 2;
}
