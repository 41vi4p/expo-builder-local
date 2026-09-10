#include "clean.hpp"

#include <iostream>
#include <string>

#include "../color.hpp"
#include "../config_store.hpp"
#include "../docker_client.hpp"

namespace ebl::commands {

namespace {

void printUsage() {
  std::cout << R"(ebl clean [options]

Removes ebl's own stopped build containers (the disposable per-build containers
`ebl build` creates, left behind if a build was interrupted or the client
disconnected before cleanup). With --all, also removes the shared gradle/npm
cache volumes and the runner/orchestrator/web images `ebl setup`/`ebl build`
pulled — the next build/setup just re-pulls/re-creates whatever it needs, so
this is safe, just slower on the next run.

Options:
      --all                      Also remove cache volumes and pulled images,
                                  not just stopped containers
      --gradle-cache-volume <n>  Gradle cache volume name (default:
                                  expo-builder-local_gradle-cache — must match
                                  what `ebl build` used, if you customized it)
      --npm-cache-volume <n>     npm cache volume name (default:
                                  expo-builder-local_npm-cache)
      --docker-socket <path>     Docker socket path (default: /var/run/docker.sock;
                                  ignored on Windows)
  -h, --help                     Show this help
)";
}

}  // namespace

void printCleanUsage() { printUsage(); }

int runClean(int argc, char** argv) {
  bool all = false;
  std::string gradleCacheVolume = "expo-builder-local_gradle-cache";
  std::string npmCacheVolume = "expo-builder-local_npm-cache";
  std::string dockerSocket = "/var/run/docker.sock";

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
      if (arg == "--all") { all = true; continue; }
      if (arg == "--gradle-cache-volume") { gradleCacheVolume = needValue(i, "--gradle-cache-volume"); continue; }
      if (arg == "--npm-cache-volume") { npmCacheVolume = needValue(i, "--npm-cache-volume"); continue; }
      if (arg == "--docker-socket") { dockerSocket = needValue(i, "--docker-socket"); continue; }
      std::cerr << ebl::color::red("Unknown option: " + arg) << "\n";
      return 2;
    }
  } catch (const std::exception& e) {
    std::cerr << ebl::color::red(e.what()) << "\n";
    return 2;
  }

  ebl::DockerClient docker(dockerSocket);
  if (!docker.ping()) {
    std::cerr << ebl::color::red("Docker isn't reachable — is it running?") << "\n";
    return 1;
  }

  std::cout << ebl::color::bold("Removing stopped build containers...") << "\n";
  std::vector<ebl::DockerClient::BuildContainerInfo> containers;
  try {
    containers = docker.listBuildContainers();
  } catch (const std::exception& e) {
    std::cerr << ebl::color::red(e.what()) << "\n";
    return 1;
  }

  bool anyRunning = false;
  int removedContainers = 0;
  for (const auto& c : containers) {
    if (c.state == "running") {
      anyRunning = true;
      continue;
    }
    try {
      docker.removeContainer(c.id);
      removedContainers++;
    } catch (const std::exception& e) {
      std::cout << ebl::color::yellow("  Could not remove " + c.id.substr(0, 12) + ": " + e.what()) << "\n";
    }
  }
  std::cout << "  Removed " << removedContainers << " container(s).\n";

  if (!all) {
    std::cout << "\n"
              << ebl::color::green("Done.") << " Run with " << ebl::color::cyan("--all")
              << " to also clear cache volumes and pulled images.\n";
    return 0;
  }

  // Refuse rather than force a live build's cache volume out from under it —
  // Docker's own DELETE /volumes already fails on an in-use volume, but checking
  // here gives a clearer message than surfacing that raw error to the user.
  if (anyRunning) {
    std::cerr << "\n"
              << ebl::color::red(
                     "A build is currently running — refusing to remove cache volumes/images out from under it. "
                     "Wait for it to finish (or stop it) and re-run `ebl clean --all`.")
              << "\n";
    return 1;
  }

  std::cout << "\n" << ebl::color::bold("Removing cache volumes...") << "\n";
  for (const auto& vol : {gradleCacheVolume, npmCacheVolume}) {
    try {
      docker.removeVolume(vol);
      std::cout << "  Removed volume \"" << vol << "\".\n";
    } catch (const std::exception& e) {
      std::cout << ebl::color::yellow(std::string("  ") + e.what()) << "\n";
    }
  }

  auto cfg = ebl::loadConfig().value_or(ebl::EblConfig{});
  std::cout << "\n" << ebl::color::bold("Removing pulled images...") << "\n";
  for (const auto& tag : {cfg.runnerImage(), cfg.orchestratorImage(), cfg.webImage()}) {
    try {
      docker.removeImage(tag);
      std::cout << "  Removed image \"" << tag << "\".\n";
    } catch (const std::exception& e) {
      std::cout << ebl::color::yellow(std::string("  ") + e.what()) << "\n";
    }
  }

  std::cout << "\n"
            << ebl::color::green(ebl::color::bold("Done."))
            << " The next `ebl build`/`ebl setup` will re-pull/re-create whatever it needs.\n";
  return 0;
}

}  // namespace ebl::commands
