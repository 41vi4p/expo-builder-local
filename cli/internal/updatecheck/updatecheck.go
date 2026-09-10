// Package updatecheck implements a best-effort check against GitHub's
// releases API for a newer `ebl` release than the running version.
// Rate-limited via a small cache file (config.Dir()/update-check.json) -
// only actually hits the network if the last check was more than 24h ago;
// otherwise reuses the cached result. Direct port of
// cli/src/update_check.hpp/.cpp.
package updatecheck

import (
	"encoding/json"
	"net/http"
	"os"
	"path/filepath"
	"time"

	"github.com/41vi4p/expo-builder-local/cli/internal/config"
	"github.com/41vi4p/expo-builder-local/cli/internal/versioncompare"
)

const (
	apiURL         = "https://api.github.com/repos/41vi4p/expo-builder-local/releases/latest"
	cacheMaxAge    = 24 * time.Hour          // a version notice doesn't need to be current-to-the-minute
	requestTimeout = 2500 * time.Millisecond // must never make a command feel slow
)

func cacheFilePath() (string, error) {
	dir, err := config.Dir()
	if err != nil {
		return "", err
	}
	return filepath.Join(dir, "update-check.json"), nil
}

// fetchLatestTagFromGitHub does a single, short-timeout GET against GitHub's
// public releases API - deliberately not routed through dockertransport
// (that's purpose-built for the Docker Engine API's local unix-socket/
// named-pipe transport, not general internet HTTPS). Returns the release's
// tag_name ("v0.22.0") verbatim, or ("", false) on any failure - network
// error, non-200, rate limiting, malformed JSON.
func fetchLatestTagFromGitHub() (string, bool) {
	client := http.Client{Timeout: requestTimeout}
	req, err := http.NewRequest(http.MethodGet, apiURL, nil)
	if err != nil {
		return "", false
	}
	// GitHub's API requires *some* User-Agent or it 403s the request outright.
	req.Header.Set("User-Agent", "ebl-cli-update-check")

	resp, err := client.Do(req)
	if err != nil {
		return "", false
	}
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		return "", false
	}

	var parsed struct {
		TagName string `json:"tag_name"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&parsed); err != nil {
		return "", false
	}
	if parsed.TagName == "" {
		return "", false
	}
	return parsed.TagName, true
}

type cacheData struct {
	LastCheckedAt int64  `json:"lastCheckedAt"`
	LatestVersion string `json:"latestVersion"`
}

func readCache(path string) cacheData {
	data, err := os.ReadFile(path)
	if err != nil {
		return cacheData{}
	}
	var cache cacheData
	// Corrupt/foreign cache file - treat exactly like "never checked before".
	_ = json.Unmarshal(data, &cache)
	return cache
}

func writeCache(path string, cache cacheData) {
	// Best-effort only - a failed write just means the next run re-checks
	// sooner than it strictly needed to. Never treated as this feature
	// failing.
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return
	}
	data, err := json.Marshal(cache)
	if err != nil {
		return
	}
	_ = os.WriteFile(path, data, 0o644)
}

// CheckForNewerVersion returns the latest version string (without the
// leading "v") only if it's genuinely newer than currentVersion; ("", false)
// if up to date, if nothing's cached yet and the network check itself
// failed, or on any other failure (offline, GitHub unreachable, rate
// limited, malformed response) - this is purely a courtesy notice, never a
// hard dependency, so it never blocks longer than a couple of seconds.
func CheckForNewerVersion(currentVersion string) (string, bool) {
	path, err := cacheFilePath()
	if err != nil {
		return "", false // e.g. %APPDATA% unset - config.Dir() errors in that case
	}

	cache := readCache(path)
	now := time.Now().Unix()

	if now-cache.LastCheckedAt > int64(cacheMaxAge.Seconds()) {
		// lastCheckedAt always advances, even on failure - otherwise an
		// extended offline stretch or a GitHub outage would mean every
		// single invocation keeps retrying (and eating the timeout) instead
		// of backing off for the full window like a successful check would.
		// Only latestVersion is conditional - a failed fetch keeps whatever
		// was last known good rather than blanking it.
		if fetched, ok := fetchLatestTagFromGitHub(); ok {
			cache.LatestVersion = fetched
		}
		cache.LastCheckedAt = now
		writeCache(path, cache)
	}

	if cache.LatestVersion == "" || !versioncompare.IsNewer(cache.LatestVersion, currentVersion) {
		return "", false
	}
	return cache.LatestVersion, true
}
