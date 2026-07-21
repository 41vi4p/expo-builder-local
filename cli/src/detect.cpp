#include "detect.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "json.hpp"

namespace fs = std::filesystem;

namespace ebl {

namespace {

bool readFile(const std::string& path, std::string& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  std::ostringstream ss;
  ss << file.rdbuf();
  out = ss.str();
  return true;
}

}  // namespace

ExpoProjectInfo detectExpoProject(const std::string& dirPath) {
  ExpoProjectInfo info;

  fs::path pkgPath = fs::path(dirPath) / "package.json";
  std::string pkgText;
  if (!readFile(pkgPath.string(), pkgText)) {
    info.reason = "No package.json found in " + dirPath;
    return info;
  }

  Json pkg;
  try {
    pkg = Json::parse(pkgText);
  } catch (const JsonError& e) {
    info.reason = std::string("package.json exists but could not be parsed: ") + e.what();
    return info;
  }

  bool hasExpoDep = pkg.get("dependencies").contains("expo") || pkg.get("devDependencies").contains("expo");
  if (!hasExpoDep) {
    info.reason = "package.json has no 'expo' dependency";
    return info;
  }

  info.isExpoProject = true;
  info.name = pkg.get("name").asString();
  info.version = pkg.get("version").asString();

  fs::path easPath = fs::path(dirPath) / "eas.json";
  std::string easText;
  if (readFile(easPath.string(), easText)) {
    try {
      Json eas = Json::parse(easText);
      Json build = eas.get("build");
      if (build.isObject()) {
        for (const auto& [profileName, _] : build.members()) info.easProfiles.push_back(profileName);
      }
    } catch (const JsonError&) {
      // malformed eas.json just means no profile list — not fatal for detection
    }
  }

  return info;
}

}  // namespace ebl
