#include "start.hpp"

#include <unistd.h>

#include <chrono>
#include <iostream>
#include <thread>

#include "../color.hpp"
#include "../config_store.hpp"
#include "../docker_client.hpp"
#include "../http_client.hpp"

namespace ebl::commands {

namespace {

constexpr const char* kNetworkName = "ebl-network";
constexpr const char* kOrchestratorContainer = "ebl-orchestrator";
constexpr const char* kWebContainer = "ebl-web";
constexpr const char* kDataVolume = "ebl-orchestrator-data";
constexpr const char* kGradleCacheVolume = "expo-builder-local_gradle-cache";
constexpr const char* kNpmCacheVolume = "expo-builder-local_npm-cache";

void printStartUsageImpl() {
  std::cout << R"(ebl start

Starts the orchestrator + web GUI as Docker containers (pulling their images if
needed) using the settings saved by `ebl config`. No git checkout or
docker-compose.yml required — this drives the containers directly.

Options:
  -h, --help   Show this help
)";
}

void printStopUsageImpl() {
  std::cout << R"(ebl stop

Stops and removes the orchestrator + web GUI containers started by `ebl start`.
Build history/keystores are preserved (they live in a separate Docker volume).

Options:
  -h, --help   Show this help
)";
}

/** Pulls an image if it isn't already present locally. Unlike the runner image,
 * there's no local-build fallback here — the orchestrator/web images are meant to
 * be pre-built and published; before they're published, build them from this repo
 * checkout (`docker compose build`) so they exist locally for `ebl start` to find. */
void ensureServiceImage(ebl::DockerClient& docker, const std::string& tag, const char* friendlyName) {
  if (docker.imageExists(tag)) return;
  std::cout << ebl::color::dim("Pulling " + std::string(friendlyName) + " image (" + tag + ")...") << "\n";
  docker.pullImage(tag, [](const std::string& line) { std::cout << line << std::flush; });
}

bool waitForHealth(const std::string& url, int attempts, int delayMs) {
  for (int i = 0; i < attempts; i++) {
    if (ebl::httpGetTcp(url).status == 200) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
  }
  return false;
}

}  // namespace

void printStartUsage() { printStartUsageImpl(); }
void printStopUsage() { printStopUsageImpl(); }

int runStart(int argc, char** argv) {
  for (int i = 0; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      printStartUsageImpl();
      return 0;
    }
  }

  auto maybeCfg = ebl::loadConfig();
  if (!maybeCfg) {
    std::cerr << ebl::color::red("No configuration found.") << " Run " << ebl::color::cyan("ebl config")
              << " first.\n";
    return 1;
  }
  ebl::EblConfig cfg = *maybeCfg;
  if (cfg.projectsRoot.empty()) {
    std::cerr << ebl::color::red("No projects folder configured.") << " Run " << ebl::color::cyan("ebl config")
              << " first.\n";
    return 1;
  }

  ebl::DockerClient docker("/var/run/docker.sock");
  if (!docker.ping()) {
    std::cerr << ebl::color::red("Docker isn't reachable.") << " Run " << ebl::color::cyan("ebl setup") << " first.\n";
    return 1;
  }

  try {
    std::cout << ebl::color::bold("Preparing images...") << "\n";
    ensureServiceImage(docker, cfg.orchestratorImage(), "orchestrator");
    ensureServiceImage(docker, cfg.webImage(), "web");

    std::cout << ebl::color::bold("Starting orchestrator...") << "\n";
    ebl::ServiceContainerSpec orchestratorSpec;
    orchestratorSpec.name = kOrchestratorContainer;
    orchestratorSpec.image = cfg.orchestratorImage();
    orchestratorSpec.network = kNetworkName;
    orchestratorSpec.portBindings = {{"4001/tcp", std::to_string(cfg.orchestratorPort)}};
    orchestratorSpec.binds = {
        "/var/run/docker.sock:/var/run/docker.sock",
        // Same path on both sides: the orchestrator talks to the *host* Docker
        // daemon over the mounted socket (a sibling container, not a nested one),
        // so any path it hands to the daemon for a build container's bind mount
        // must already be a real host path — not remapped inside this container.
        cfg.projectsRoot + ":" + cfg.projectsRoot,
        std::string(kDataVolume) + ":/data",
    };
    orchestratorSpec.env = {
        "PORT=4001",
        "HOST=0.0.0.0",
        "CORS_ORIGIN=*",
        "DATA_DIR=/data",
        "DOCKER_SOCKET=/var/run/docker.sock",
        "RUNNER_IMAGE=" + cfg.runnerImage(),
        std::string("GRADLE_CACHE_VOLUME=") + kGradleCacheVolume,
        std::string("NPM_CACHE_VOLUME=") + kNpmCacheVolume,
        "ALLOWED_ROOTS=" + cfg.projectsRoot,
        "HOST_UID=" + std::to_string(getuid()),
        "HOST_GID=" + std::to_string(getgid()),
        "MASTER_KEY=" + cfg.masterKey,
        "MAX_CONCURRENT_BUILDS=1",
    };
    if (!cfg.expoToken.empty()) orchestratorSpec.env.push_back("EXPO_TOKEN=" + cfg.expoToken);

    std::string orchestratorId = docker.createServiceContainer(orchestratorSpec);
    docker.startContainer(orchestratorId);

    std::cout << ebl::color::bold("Starting web GUI...") << "\n";
    ebl::ServiceContainerSpec webSpec;
    webSpec.name = kWebContainer;
    webSpec.image = cfg.webImage();
    webSpec.network = kNetworkName;
    webSpec.portBindings = {{"3000/tcp", std::to_string(cfg.webPort)}};
    webSpec.env = {"ORCHESTRATOR_URL=http://localhost:" + std::to_string(cfg.orchestratorPort)};

    std::string webId = docker.createServiceContainer(webSpec);
    docker.startContainer(webId);

    std::string orchestratorUrl = "http://localhost:" + std::to_string(cfg.orchestratorPort);
    std::string webUrl = "http://localhost:" + std::to_string(cfg.webPort);

    std::cout << ebl::color::dim("Waiting for the orchestrator to come online...") << "\n";
    bool orchestratorUp = waitForHealth(orchestratorUrl + "/api/health", 30, 1000);
    bool webUp = waitForHealth(webUrl, 30, 1000);

    std::cout << "\n";
    std::cout << "  Orchestrator: " << orchestratorUrl << "  "
              << (orchestratorUp ? ebl::color::green("[online]") : ebl::color::red("[not responding]")) << "\n";
    std::cout << "  Web GUI:      " << webUrl << "  "
              << (webUp ? ebl::color::green("[online]") : ebl::color::red("[not responding]")) << "\n\n";

    if (!orchestratorUp || !webUp) {
      std::cout << ebl::color::yellow(
                        "One or more services didn't respond in time. Check `docker logs " +
                        std::string(kOrchestratorContainer) + "` / `docker logs " + kWebContainer + "`.")
                << "\n";
      return 1;
    }

    std::cout << ebl::color::green(ebl::color::bold("Everything's up.")) << " Open " << ebl::color::cyan(webUrl)
              << " to use the GUI, or run " << ebl::color::cyan("ebl build .") << " from a project folder.\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << ebl::color::red(std::string("Failed to start: ") + e.what()) << "\n";
    return 1;
  }
}

int runStop(int argc, char** argv) {
  for (int i = 0; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      printStopUsageImpl();
      return 0;
    }
  }

  ebl::DockerClient docker("/var/run/docker.sock");
  if (!docker.ping()) {
    std::cerr << ebl::color::red("Docker isn't reachable.") << "\n";
    return 1;
  }

  try {
    docker.removeContainerByName(kWebContainer);
    docker.removeContainerByName(kOrchestratorContainer);
    std::cout << ebl::color::green("Stopped.") << " (Build history and keystores are preserved.)\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << ebl::color::red(std::string("Failed to stop: ") + e.what()) << "\n";
    return 1;
  }
}

}  // namespace ebl::commands
