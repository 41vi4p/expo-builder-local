#pragma once
// Local, persisted CLI configuration — the result of `ebl config` — read by `ebl
// start` (and consulted by `ebl setup`). Lives at ~/.config/ebl/config.json (0600),
// with the Expo token and the orchestrator's generated MASTER_KEY encrypted at rest
// using a machine-local key at ~/.config/ebl/machine.key (0600, generated on first
// use) — see crypto.hpp. Neither file is ever meant to leave this machine.
#include <cstdint>
#include <optional>
#include <string>

namespace ebl {

struct EblConfig {
  std::string dockerHubNamespace = "ebllocal";
  std::string projectsRoot;
  int orchestratorPort = 4001;
  int webPort = 3000;
  std::string masterKey;   // plaintext once loaded into memory; encrypted on disk
  std::string expoToken;   // plaintext once loaded into memory; encrypted on disk (may be empty)
  int64_t setupCompletedAt = 0;  // 0 = setup has never completed

  std::string runnerImage() const { return dockerHubNamespace + "/expo-builder-local-runner:latest"; }
  std::string orchestratorImage() const { return dockerHubNamespace + "/expo-builder-local-orchestrator:latest"; }
  std::string webImage() const { return dockerHubNamespace + "/expo-builder-local-web:latest"; }
};

std::string configDir();
std::string configFilePath();

/** Returns nullopt if no config has been saved yet (i.e. `ebl config` was never run). */
std::optional<EblConfig> loadConfig();

/** Writes the config file (and machine key, if this is the first save) with 0600
 * permissions. Generates masterKey automatically if it's still empty. */
void saveConfig(EblConfig& config);

}  // namespace ebl
