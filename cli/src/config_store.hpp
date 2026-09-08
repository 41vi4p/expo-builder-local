#pragma once
// Local, persisted CLI configuration - the result of `ebl config` - read by `ebl
// start` (and consulted by `ebl setup`). Lives at ~/.config/ebl/config.json (0600),
// with the Expo token and the orchestrator's generated MASTER_KEY encrypted at rest
// using a machine-local key at ~/.config/ebl/machine.key (0600, generated on first
// use) - see crypto.hpp. Neither file is ever meant to leave this machine.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ebl {

/** A saved token for one EAS account (app.json's `expo.owner` slug, e.g.
 * "project-cell"). `owner` is never empty here - the single unscoped fallback lives
 * in EblConfig::expoToken instead, so there's exactly one place a "no owner
 * matched" token comes from. */
struct ExpoTokenEntry {
  std::string owner;
  std::string token;  // plaintext once loaded into memory; encrypted on disk
};

/** Windows-only native build engine's provisioned toolchain (see
 * cli/src/native_toolchain.hpp) - installs the Android SDK/JDK/Node directly on the
 * host instead of using a Docker container. Each *InstalledByEbl flag is per
 * component, not a single global one, because `ebl setup --runtime native` prefers
 * an already-installed toolchain component over downloading its own copy - only the
 * components it actually downloaded should ever be removed by an uninstall/cleanup,
 * never something the user already had (e.g. an existing Android Studio SDK). */
struct NativeToolchainConfig {
  std::string jdkHome;              // empty if not provisioned
  bool jdkInstalledByEbl = false;
  std::string androidSdkRoot;
  bool androidSdkInstalledByEbl = false;
  std::string nodeHome;
  bool nodeInstalledByEbl = false;
};

struct EblConfig {
  std::string projectsRoot;
  int orchestratorPort = 4001;
  int webPort = 3000;
  std::string masterKey;   // plaintext once loaded into memory; encrypted on disk
  std::string expoToken;   // default/fallback token, used when no owner-specific entry matches (may be empty)
  std::vector<ExpoTokenEntry> expoTokensByOwner;
  int64_t setupCompletedAt = 0;  // 0 = setup has never completed

  // Windows-only: "docker" or "native" (see ../CLAUDE.md's native-engine section).
  // Empty means "never chosen yet" - `ebl setup`/`ebl build` default it to "native"
  // on Windows the first time, "docker" everywhere else (the only valid value on
  // non-Windows). Not encrypted - no secret material, same as runnerImage() etc.
  std::string buildMode;
  NativeToolchainConfig nativeToolchain;

  // All three always come from the canonical upstream account - this used to be
  // configurable (a "Docker Hub namespace" field/prompt in `ebl config`), but that
  // was removed: nobody actually needs to point this at a different account, and
  // it just added an unused prompt to the setup wizard. Hardcode a new
  // --runner-image/--orchestrator-image-style flag instead if a real need for a
  // custom namespace ever comes up.
  std::string runnerImage() const { return "41vi4p/expo-builder-local-runner:latest"; }
  std::string orchestratorImage() const { return "41vi4p/expo-builder-local-orchestrator:latest"; }
  std::string webImage() const { return "41vi4p/expo-builder-local-web:latest"; }

  /** Resolves the token to use for a project whose app.json declares `owner`
   * (empty string if it doesn't declare one) - an exact owner match wins, otherwise
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
