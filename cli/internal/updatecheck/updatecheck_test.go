package updatecheck

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestCheckForNewerVersionRealNetworkCall(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())

	// A real network call against GitHub's public API - cheap (one request,
	// 2.5s max timeout) and this is exactly the kind of best-effort,
	// never-fails-the-command behavior this package exists to guarantee;
	// worth actually exercising once rather than only against mocks.
	latest, isNewer := CheckForNewerVersion("0.0.1") // deliberately ancient, so any real release is "newer"
	t.Logf("CheckForNewerVersion(\"0.0.1\") = (%q, %v)", latest, isNewer)

	// Whatever GitHub returns, a cache file must now exist (rate-limiting
	// depends on it).
	path, err := cacheFilePath()
	if err != nil {
		t.Fatalf("cacheFilePath: %v", err)
	}
	if _, err := os.Stat(path); err != nil {
		t.Errorf("expected a cache file to have been written at %s: %v", path, err)
	}
}

func TestCachedResultIsReusedWithinTheWindow(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	path, err := cacheFilePath()
	if err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatal(err)
	}
	cache := cacheData{LastCheckedAt: time.Now().Unix(), LatestVersion: "v99.0.0"}
	data, _ := json.Marshal(cache)
	if err := os.WriteFile(path, data, 0o644); err != nil {
		t.Fatal(err)
	}

	// The cache was just written (well within the 24h window), so this must
	// NOT hit the network - it should return the cached "v99.0.0" directly,
	// which is newer than any currentVersion we pass.
	latest, isNewer := CheckForNewerVersion("1.0.0")
	if !isNewer || latest != "v99.0.0" {
		t.Errorf("latest=%q isNewer=%v, want v99.0.0/true from the fresh cache", latest, isNewer)
	}
}

func TestNotNewerWhenCachedVersionIsNotActuallyNewer(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	path, err := cacheFilePath()
	if err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatal(err)
	}
	cache := cacheData{LastCheckedAt: time.Now().Unix(), LatestVersion: "v1.0.0"}
	data, _ := json.Marshal(cache)
	if err := os.WriteFile(path, data, 0o644); err != nil {
		t.Fatal(err)
	}

	_, isNewer := CheckForNewerVersion("2.0.0") // already newer than the cached "latest"
	if isNewer {
		t.Error("expected isNewer = false when the current version is already ahead of the cache")
	}
}

func TestCorruptCacheIsTreatedAsNeverChecked(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	path, err := cacheFilePath()
	if err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte("not valid json"), 0o644); err != nil {
		t.Fatal(err)
	}

	// Must not panic or error out - a corrupt cache just means a fresh
	// (real, rate-limited-safe) check happens.
	CheckForNewerVersion("1.0.0")
}
