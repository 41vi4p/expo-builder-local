#pragma once
// Same detection rule as orchestrator/src/build/detect.ts, reimplemented here so the
// CLI has no runtime dependency on the orchestrator/GUI (or Node) being installed.
#include <optional>
#include <string>
#include <vector>

namespace ebl {

struct ExpoProjectInfo {
  bool isExpoProject = false;
  std::string name;
  std::string version;
  std::vector<std::string> easProfiles;
  std::string owner;   // app.json's expo.owner (EAS account slug) — empty if unset
  std::string reason;  // populated when isExpoProject is false
};

ExpoProjectInfo detectExpoProject(const std::string& dirPath);

}  // namespace ebl
