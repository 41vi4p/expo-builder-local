#include "config_store.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "base64.hpp"
#include "crypto.hpp"
#include "json.hpp"

namespace ebl {

namespace {

#ifndef _WIN32
std::string homeDir() {
  if (const char* h = std::getenv("HOME")) return h;
  if (struct passwd* pw = getpwuid(getuid())) return pw->pw_dir;
  throw std::runtime_error("Could not determine home directory (HOME is unset)");
}
#endif

std::string machineKeyPath() { return configDir() + "/machine.key"; }

AesKey loadOrCreateMachineKey() {
  std::string path = machineKeyPath();
  std::ifstream in(path, std::ios::binary);
  if (in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string decoded = base64Decode(ss.str());
    if (decoded.size() == 32) {
      AesKey key{};
      std::copy(decoded.begin(), decoded.end(), key.begin());
      return key;
    }
    // Fall through and regenerate if the file is corrupt/wrong size — better than
    // hard-failing every command forever because of one bad write.
  }

  AesKey key = generateAesKey();
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) throw std::runtime_error("Could not write machine key to " + path);
  out << base64Encode(std::string(reinterpret_cast<const char*>(key.data()), key.size()));
  out.close();
#ifndef _WIN32
  ::chmod(path.c_str(), S_IRUSR | S_IWUSR);
#endif
  // On Windows, secret files rely on per-user profile isolation (%APPDATA% is
  // already private to the owning account) rather than an explicit ACL tighten.
  return key;
}

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return "";
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// std::rename() (POSIX rename(2) on Unix) atomically replaces an existing
// destination; the plain C rename() on Windows instead fails with EEXIST if
// `path` already exists — which it always will here from the second save
// onward. MoveFileExA with MOVEFILE_REPLACE_EXISTING is the Windows equivalent
// of "write tmp, then atomically swap it in".
void atomicReplace(const std::string& tmpPath, const std::string& path) {
#ifdef _WIN32
  if (!::MoveFileExA(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    throw std::runtime_error("Could not save config to " + path);
  }
#else
  if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
    throw std::runtime_error("Could not save config to " + path);
  }
#endif
}

}  // namespace

std::string configDir() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
    return std::string(xdg) + "/ebl";
  }
#ifdef _WIN32
  if (const char* appData = std::getenv("APPDATA")) {
    return std::string(appData) + "\\ebl";
  }
  throw std::runtime_error("Could not determine a config directory (%APPDATA% is unset)");
#else
  return homeDir() + "/.config/ebl";
#endif
}

std::string configFilePath() { return configDir() + "/config.json"; }

std::optional<EblConfig> loadConfig() {
  std::string text = readFile(configFilePath());
  if (text.empty()) return std::nullopt;

  Json root = Json::parse(text);
  EblConfig cfg;
  // dockerHubNamespace is no longer read (see config_store.hpp) — older config
  // files may still have the key; Json::get() on an unrecognized key is harmless.
  cfg.projectsRoot = root.get("projectsRoot").asString();
  cfg.orchestratorPort = static_cast<int>(root.get("orchestratorPort").asInt(cfg.orchestratorPort));
  cfg.webPort = static_cast<int>(root.get("webPort").asInt(cfg.webPort));
  cfg.setupCompletedAt = root.get("setupCompletedAt").asInt(0);

  AesKey key = loadOrCreateMachineKey();
  std::string masterKeyEnc = root.get("masterKeyEnc").asString();
  std::string expoTokenEnc = root.get("expoTokenEnc").asString();
  if (!masterKeyEnc.empty()) cfg.masterKey = aesDecrypt(masterKeyEnc, key);
  if (!expoTokenEnc.empty()) cfg.expoToken = aesDecrypt(expoTokenEnc, key);

  Json tokensByOwner = root.get("expoTokensByOwner");
  if (tokensByOwner.isArray()) {
    for (size_t i = 0; i < tokensByOwner.size(); i++) {
      const Json& entry = tokensByOwner.at(i);
      std::string owner = entry.get("owner").asString();
      std::string tokenEnc = entry.get("tokenEnc").asString();
      if (owner.empty() || tokenEnc.empty()) continue;
      cfg.expoTokensByOwner.push_back({owner, aesDecrypt(tokenEnc, key)});
    }
  }

  return cfg;
}

void saveConfig(EblConfig& config) {
  std::string dir = configDir();
#ifdef _WIN32
  if (!::CreateDirectoryA(dir.c_str(), nullptr) && ::GetLastError() != ERROR_ALREADY_EXISTS) {
    throw std::runtime_error("Could not create config directory " + dir);
  }
#else
  if (::mkdir(dir.c_str(), S_IRWXU) != 0 && errno != EEXIST) {
    throw std::runtime_error("Could not create config directory " + dir);
  }
  ::chmod(dir.c_str(), S_IRWXU);
#endif

  if (config.masterKey.empty()) {
    AesKey generated = generateAesKey();
    config.masterKey = base64Encode(std::string(reinterpret_cast<const char*>(generated.data()), generated.size()));
  }

  AesKey key = loadOrCreateMachineKey();

  Json root = Json::object();
  root.set("projectsRoot", Json(config.projectsRoot));
  root.set("orchestratorPort", Json(config.orchestratorPort));
  root.set("webPort", Json(config.webPort));
  root.set("setupCompletedAt", Json(static_cast<double>(config.setupCompletedAt)));
  root.set("masterKeyEnc", Json(aesEncrypt(config.masterKey, key)));
  if (!config.expoToken.empty()) root.set("expoTokenEnc", Json(aesEncrypt(config.expoToken, key)));

  Json tokensByOwner = Json::array();
  for (const auto& entry : config.expoTokensByOwner) {
    if (entry.owner.empty() || entry.token.empty()) continue;
    Json obj = Json::object();
    obj.set("owner", Json(entry.owner));
    obj.set("tokenEnc", Json(aesEncrypt(entry.token, key)));
    tokensByOwner.push_back(obj);
  }
  root.set("expoTokensByOwner", tokensByOwner);

  // Write to a sibling temp file and rename() over the real path (atomic on the
  // same filesystem), rather than truncating the real file in place — a crash,
  // kill, or ENOSPC partway through an in-place write leaves a corrupt/empty
  // config.json (as happened here from a disk-full condition mid-save); a rename
  // can only ever land the old file or the fully-written new one, never a partial.
  std::string path = configFilePath();
  std::string tmpPath = path + ".tmp";
  {
    std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Could not write config to " + tmpPath);
    std::string serialized = root.dump();
    out << serialized;
    out.flush();
    if (!out) throw std::runtime_error("Failed writing config to " + tmpPath + " (disk full?)");
  }
#ifndef _WIN32
  ::chmod(tmpPath.c_str(), S_IRUSR | S_IWUSR);
#endif
  atomicReplace(tmpPath, path);
}

}  // namespace ebl
