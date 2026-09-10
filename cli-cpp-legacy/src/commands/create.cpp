#include "create.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "../color.hpp"
#include "../host_process.hpp"

namespace fs = std::filesystem;

namespace ebl::commands {

namespace {

void printUsage() {
  std::cout << R"(ebl create <path> <name> [options]

Scaffolds a brand-new Expo app named <name> inside directory <path> (e.g.
`ebl create . myapp` creates ./myapp), via `npx create-expo-app` - runs directly
on this machine, not inside a Docker container (scaffolding needs nothing that a
disposable build container provides; only `ebl build` actually needs one).

Arguments:
  path                     Directory the new app's folder will be created inside
  name                     Name of the new app (and its folder)

Options:
      --template <name>          Passed straight through to create-expo-app's own
                                  --template flag (a template name or npm package)
  -h, --help                     Show this help

Requires Node.js (for `npx`) on PATH - if you don't have it, install it from
https://nodejs.org first.
)";
}

}  // namespace

void printCreateUsage() { printUsage(); }

int runCreate(int argc, char** argv) {
  std::string path;
  std::string name;
  std::optional<std::string> templateName;
  bool sawPath = false;
  bool sawName = false;

  auto needValue = [&](int& i, const char* flagName) -> std::string {
    if (i + 1 >= argc) throw std::runtime_error(std::string("Missing value for ") + flagName);
    return argv[++i];
  };

  try {
    for (int i = 0; i < argc; i++) {
      std::string arg = argv[i];
      if (arg == "-h" || arg == "--help") {
        printUsage();
        return 0;
      }
      if (arg == "--template") {
        templateName = needValue(i, "--template");
        continue;
      }
      if (!arg.empty() && arg[0] == '-') {
        std::cerr << ebl::color::red("Unknown option: " + arg) << "\n";
        return 2;
      }
      if (!sawPath) {
        path = arg;
        sawPath = true;
      } else if (!sawName) {
        name = arg;
        sawName = true;
      } else {
        std::cerr << ebl::color::red("Unexpected extra argument: " + arg) << "\n";
        return 2;
      }
    }
  } catch (const std::exception& e) {
    std::cerr << ebl::color::red(e.what()) << "\n";
    return 2;
  }

  if (!sawPath || !sawName) {
    std::cerr << ebl::color::red("Usage: ebl create <path> <name> [options]") << "\n";
    return 2;
  }

  fs::path targetDir = fs::absolute(path).lexically_normal();
  if (!fs::exists(targetDir) || !fs::is_directory(targetDir)) {
    std::cerr << ebl::color::red("Not a directory: " + targetDir.string()) << "\n";
    return 2;
  }

  fs::path appDir = targetDir / name;
  if (fs::exists(appDir)) {
    std::cerr << ebl::color::red(appDir.string() + " already exists.") << "\n";
    return 2;
  }

  std::vector<std::string> args = {"npx", "create-expo-app@latest", name};
  if (templateName) {
    args.push_back("--template");
    args.push_back(*templateName);
  }

  std::cout << ebl::color::bold("Creating " + ebl::color::cyan(appDir.string())) << "\n\n";

  int exitCode = ebl::runHostProcessStreaming(args, targetDir.string(),
                                               [](const char* data, size_t len) { std::cout.write(data, static_cast<std::streamsize>(len)); });

  if (exitCode == 127) {
    std::cerr << "\n"
               << ebl::color::red(
                      "`npx` not found - install Node.js from https://nodejs.org, then try again.")
               << "\n";
    return 1;
  }
  if (exitCode != 0) {
    std::cerr << "\n" << ebl::color::red("create-expo-app exited with status " + std::to_string(exitCode)) << "\n";
    return 1;
  }

  std::cout << "\n"
            << ebl::color::green(ebl::color::bold("Created " + appDir.string())) << "\n"
            << ebl::color::dim("Next:") << "\n"
            << "  cd " << name << "\n"
            << "  ebl setup        " << ebl::color::dim("(first time only)") << "\n"
            << "  ebl build .\n";
  return 0;
}

}  // namespace ebl::commands
