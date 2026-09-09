#pragma once
#include <optional>
#include <string>

namespace ebl {

/** Best-effort check against GitHub's releases API for a newer `ebl` release than
 * `currentVersion`. Rate-limited via a small cache file
 * (`configDir()/update-check.json`) — only actually hits the network if the last
 * check was more than 24h ago; otherwise reuses the cached result. Returns the
 * latest version string (without the leading "v") only if it's genuinely newer than
 * `currentVersion`; nullopt if up to date, if nothing's cached yet and the network
 * check itself failed, or on any other failure (offline, GitHub unreachable, rate
 * limited, malformed response) — this is purely a courtesy notice, never a hard
 * dependency, so it never throws and never blocks longer than a couple of seconds. */
std::optional<std::string> checkForNewerVersion(const std::string& currentVersion);

}  // namespace ebl
