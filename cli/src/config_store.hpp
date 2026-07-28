#pragma once
// Local, persisted CLI configuration — the result of `ebl config` — read by `ebl
// start` (and consulted by `ebl setup`). Lives at ~/.config/ebl/config.json (0600),
// with the Expo token and the orchestrator's generated MASTER_KEY encrypted at rest
// using a machine-local key at ~/.config/ebl/machine.key (0600, generated on first
// use) — see crypto.hpp. Neither file is ever meant to leave this machine.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ebl {

/** A saved token for one EAS account (app.json's `expo.owner` slug, e.g.
 * "project-cell"). `owner` is never empty here — the single unscoped fallback lives
 * in EblConfig::expoToken instead, so there's exactly one place a "no owner
 * matched" token comes from. */
struct ExpoTokenEntry {
  std::string owner;
  std::string token;  // plaintext once loaded into memory; encrypted on disk
};

struct EblConfig {
  std::string dockerHubNamespace = "41vi4p";
  std::string projectsRoot;
  int orchestratorPort = 4001;
  int webPort = 3000;
  std::string masterKey;   // plaintext once loaded into memory; encrypted on disk
  std::string expoToken;   // default/fallback token, used when no owner-specific entry matches (may be empty)
  std::vector<ExpoTokenEntry> expoTokensByOwner;
  int64_t setupCompletedAt = 0;  // 0 = setup has never completed

  std::string runnerImage() const { return dockerHubNamespace + "/expo-builder-local-runner:latest"; }
  std::string orchestratorImage() const { return dockerHubNamespace + "/expo-builder-local-orchestrator:latest"; }
  std::string webImage() const { return dockerHubNamespace + "/expo-builder-local-web:latest"; }

  /** Resolves the token to use for a project whose app.json declares `owner`
   * (empty string if it doesn't declare one) — an exact owner match wins, otherwise
   * falls back to the single default `expoToken`. */
  std::string expoTokenFor(const std::string& owner) const {
    if (!owner.empty()) {
      for (const auto& entry : expoTokensByOwner) {
        if (entry.owner == owner) return entry.token;
      }
    }
    return expoToken;
  }
};

std::string configDir();
std::string configFilePath();

/** Returns nullopt if no config has been saved yet (i.e. `ebl config` was never run). */
std::optional<EblConfig> loadConfig();

/** Writes the config file (and machine key, if this is the first save) with 0600
 * permissions. Generates masterKey automatically if it's still empty. */
void saveConfig(EblConfig& config);

}  // namespace ebl
