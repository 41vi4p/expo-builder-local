#include "update.hpp"

#include <iostream>
#include <string>

#include "../color.hpp"
#include "../config_store.hpp"
#include "../docker_client.hpp"
#include "../pull_progress.hpp"
#include "../runner_context.hpp"

namespace ebl::commands {

namespace {

void printUsage() {
  std::cout << R"(ebl update [options]

Force-refreshes the runner/orchestrator/web images right now, unconditionally.

`ebl build`/`ebl start` already pull on every run, but a plain pull only ever
transfers layers that actually changed upstream — it can't fix an image whose
published tag itself was built from a stale Docker layer cache (e.g. eas-cli
inside the runner image resolving "latest" once, then every later build/publish
of that same layer silently reusing that old resolution). `ebl update` is for
when you want to be certain right now: it always re-pulls all three images, and
for the runner image specifically, if pulling isn't possible at all (offline, or
a custom --runner-image that was never published), it rebuilds it from the
bundled context with Docker's build cache fully disabled (nocache + a forced
re-pull of the base image), not a normal cached build.

This doesn't remove the images it's replacing — Docker leaves the old, now-
untagged layers on disk as reclaimable space; run `ebl clean --all` (or `docker
image prune`) afterward if you want that space back.

Options:
      --runner-image <tag>        (default: 41vi4p/expo-builder-local-runner:latest)
      --orchestrator-image <tag>  (default: 41vi4p/expo-builder-local-orchestrator:latest)
      --web-image <tag>           (default: 41vi4p/expo-builder-local-web:latest)
      --docker-socket <path>      Docker socket path (default: /var/run/docker.sock;
                                  ignored on Windows)
  -h, --help                      Show this help
)";
}

bool pullFresh(ebl::DockerClient& docker, const std::string& tag, const char* friendlyName) {
  std::cout << ebl::color::dim(std::string("Pulling ") + friendlyName + " image (" + tag + ")...") << "\n";
  try {
    ebl::PullProgressRenderer progress;
    docker.pullImage(tag, [&progress](const std::string& id, const std::string& status, const std::string& p) {
      progress.onEvent(id, status, p);
    });
    return true;
  } catch (const std::exception& e) {
    std::cout << ebl::color::yellow(std::string("  Pull failed: ") + e.what()) << "\n";
    return false;
  }
}

}  // namespace

void printUpdateUsage() { printUsage(); }

int runUpdate(int argc, char** argv) {
  auto cfg = ebl::loadConfig().value_or(ebl::EblConfig{});
  std::string runnerImage = cfg.runnerImage();
  std::string orchestratorImage = cfg.orchestratorImage();
  std::string webImage = cfg.webImage();
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
      if (arg == "--runner-image") { runnerImage = needValue(i, "--runner-image"); continue; }
      if (arg == "--orchestrator-image") { orchestratorImage = needValue(i, "--orchestrator-image"); continue; }
      if (arg == "--web-image") { webImage = needValue(i, "--web-image"); continue; }
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

  bool anyFailed = false;

  std::cout << ebl::color::bold("Updating the runner image...") << "\n";
  if (!pullFresh(docker, runnerImage, "runner")) {
    std::cout << ebl::color::yellow("Rebuilding it from scratch instead (no cache, ~10-20 minutes)...") << "\n";
    try {
      std::string contextDir = ebl::resolveRunnerContextDir();
      docker.buildImage(
          contextDir, runnerImage, [](const std::string& line) { std::cout << line << std::flush; },
          /*noCache=*/true);
      std::cout << ebl::color::green("Runner image \"" + runnerImage + "\" rebuilt from scratch.") << "\n";
    } catch (const std::exception& e) {
      std::cout << ebl::color::red(std::string("  Rebuild failed: ") + e.what()) << "\n";
      anyFailed = true;
    }
  }

  std::cout << "\n" << ebl::color::bold("Updating the orchestrator image...") << "\n";
  if (!pullFresh(docker, orchestratorImage, "orchestrator")) {
    std::cout << ebl::color::dim(
                      "No local-build fallback for this one — it's meant to be pre-published. Build it "
                      "from this repo checkout (`docker compose build orchestrator`) if you need it locally.")
              << "\n";
    anyFailed = true;
  }

  std::cout << "\n" << ebl::color::bold("Updating the web image...") << "\n";
  if (!pullFresh(docker, webImage, "web")) {
    std::cout << ebl::color::dim(
                      "No local-build fallback for this one either — build it from this repo checkout "
                      "(`docker compose build web`) if you need it locally.")
              << "\n";
    anyFailed = true;
  }

  std::cout << "\n";
  if (anyFailed) {
    std::cout << ebl::color::yellow("Update finished with at least one image not refreshed (see above).") << "\n";
  } else {
    std::cout << ebl::color::green(ebl::color::bold("All images up to date.")) << "\n";
  }
  std::cout << "Run " << ebl::color::cyan("ebl clean --all") << " to reclaim disk space from the images just "
            << "replaced. If `ebl start` is currently running, re-run it to pick up the refreshed "
            << "orchestrator/web images.\n";
  return anyFailed ? 1 : 0;
}

}  // namespace ebl::commands
