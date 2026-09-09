#include "update_check.hpp"

#include <curl/curl.h>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "config_store.hpp"
#include "json.hpp"
#include "version_compare.hpp"

namespace fs = std::filesystem;

namespace ebl {

namespace {

constexpr const char* kApiUrl = "https://api.github.com/repos/41vi4p/expo-builder-local/releases/latest";
constexpr long long kCacheMaxAgeSeconds = 24 * 60 * 60;  // 24h — a version notice doesn't need to be current-to-the-minute
constexpr long kRequestTimeoutMs = 2500;                 // must never make a command feel slow

std::string cacheFilePath() { return configDir() + "/update-check.json"; }

size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
  return size * nmemb;
}

/** A single, short-timeout GET against GitHub's public releases API — deliberately
 * not routed through http_client.hpp's HttpClient (that's purpose-built for the
 * Docker Engine API's local unix-socket/named-pipe transport, not general internet
 * HTTPS). Returns the release's tag_name ("v0.22.0") verbatim, or nullopt on any
 * failure — network error, non-200, rate limiting, malformed JSON. */
std::optional<std::string> fetchLatestTagFromGitHub() {
  CURL* curl = curl_easy_init();
  if (!curl) return std::nullopt;

  std::string body;
  curl_easy_setopt(curl, CURLOPT_URL, kApiUrl);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  // GitHub's API requires *some* User-Agent or it 403s the request outright.
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "ebl-cli-update-check");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, kRequestTimeoutMs);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

  CURLcode res = curl_easy_perform(curl);
  long httpCode = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK || httpCode != 200) return std::nullopt;

  try {
    Json parsed = Json::parse(body);
    std::string tag = parsed.get("tag_name").asString();
    return tag.empty() ? std::nullopt : std::make_optional(tag);
  } catch (const JsonError&) {
    return std::nullopt;
  }
}

struct Cache {
  int64_t lastCheckedAt = 0;
  std::string latestVersion;
};

Cache readCache(const std::string& path) {
  Cache cache;
  std::ifstream in(path, std::ios::binary);
  if (!in) return cache;
  std::ostringstream ss;
  ss << in.rdbuf();
  try {
    Json j = Json::parse(ss.str());
    cache.lastCheckedAt = j.get("lastCheckedAt").asInt(0);
    cache.latestVersion = j.get("latestVersion").asString();
  } catch (const JsonError&) {
    // Corrupt/foreign cache file - treat exactly like "never checked before".
  }
  return cache;
}

void writeCache(const std::string& path, const Cache& cache) {
  try {
    fs::create_directories(fs::path(path).parent_path());
    Json j = Json::object();
    j.set("lastCheckedAt", Json(static_cast<double>(cache.lastCheckedAt)));
    j.set("latestVersion", Json(cache.latestVersion));
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (out) out << j.dump();
  } catch (const std::exception&) {
    // Best-effort only - a failed write just means the next run re-checks sooner
    // than it strictly needed to. Never treated as this feature failing.
  }
}

}  // namespace

std::optional<std::string> checkForNewerVersion(const std::string& currentVersion) {
  std::string path;
  try {
    path = cacheFilePath();
  } catch (const std::exception&) {
    return std::nullopt;  // e.g. %APPDATA% unset - configDir() throws in that case
  }

  Cache cache = readCache(path);
  int64_t now = static_cast<int64_t>(std::time(nullptr));

  if (now - cache.lastCheckedAt > kCacheMaxAgeSeconds) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    std::optional<std::string> fetched = fetchLatestTagFromGitHub();
    curl_global_cleanup();

    // lastCheckedAt always advances, even on failure - otherwise an extended
    // offline stretch or a GitHub outage would mean every single invocation keeps
    // retrying (and eating the timeout) instead of backing off for the full
    // window like a successful check would. Only latestVersion is conditional -
    // a failed fetch keeps whatever was last known good rather than blanking it.
    if (fetched) cache.latestVersion = *fetched;
    cache.lastCheckedAt = now;
    writeCache(path, cache);
  }

  if (cache.latestVersion.empty() || !isVersionNewer(cache.latestVersion, currentVersion)) return std::nullopt;
  return cache.latestVersion;
}

}  // namespace ebl
