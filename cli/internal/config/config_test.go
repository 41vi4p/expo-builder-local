package config

import (
	"os"
	"path/filepath"
	"testing"
)

// withXDGConfigHome points $XDG_CONFIG_HOME at dir for the duration of the
// test - every Dir()/FilePath() call in this package resolves relative to it.
func withXDGConfigHome(t *testing.T, dir string) {
	t.Helper()
	t.Setenv("XDG_CONFIG_HOME", dir)
}

func TestNewHasDocumentedDefaults(t *testing.T) {
	cfg := New()
	if cfg.OrchestratorPort != 4001 {
		t.Errorf("OrchestratorPort = %d, want 4001", cfg.OrchestratorPort)
	}
	if cfg.WebPort != 3000 {
		t.Errorf("WebPort = %d, want 3000", cfg.WebPort)
	}
}

func TestLoadReturnsNotFoundWhenNoConfigSaved(t *testing.T) {
	withXDGConfigHome(t, t.TempDir())
	_, found, err := Load()
	if err != nil {
		t.Fatalf("Load: %v", err)
	}
	if found {
		t.Error("expected found = false")
	}
}

func TestSaveThenLoadRoundTrip(t *testing.T) {
	withXDGConfigHome(t, t.TempDir())

	cfg := New()
	cfg.ProjectsRoot = "/home/dev/projects"
	cfg.ExpoToken = "default-token"
	cfg.ExpoTokensByOwner = []ExpoTokenEntry{
		{Owner: "project-cell", Token: "owner-token"},
		{Owner: "other-account", Token: "other-token"},
	}
	cfg.SetupCompletedAt = 1234567890

	if err := Save(&cfg); err != nil {
		t.Fatalf("Save: %v", err)
	}
	if cfg.MasterKey == "" {
		t.Error("expected Save to generate a MasterKey")
	}

	loaded, found, err := Load()
	if err != nil {
		t.Fatalf("Load: %v", err)
	}
	if !found {
		t.Fatal("expected found = true")
	}
	if loaded.ProjectsRoot != cfg.ProjectsRoot {
		t.Errorf("ProjectsRoot = %q, want %q", loaded.ProjectsRoot, cfg.ProjectsRoot)
	}
	if loaded.ExpoToken != cfg.ExpoToken {
		t.Errorf("ExpoToken = %q, want %q", loaded.ExpoToken, cfg.ExpoToken)
	}
	if loaded.MasterKey != cfg.MasterKey {
		t.Errorf("MasterKey = %q, want %q", loaded.MasterKey, cfg.MasterKey)
	}
	if loaded.SetupCompletedAt != cfg.SetupCompletedAt {
		t.Errorf("SetupCompletedAt = %d, want %d", loaded.SetupCompletedAt, cfg.SetupCompletedAt)
	}
	if len(loaded.ExpoTokensByOwner) != 2 {
		t.Fatalf("len(ExpoTokensByOwner) = %d, want 2", len(loaded.ExpoTokensByOwner))
	}
	if loaded.ExpoTokensByOwner[0] != cfg.ExpoTokensByOwner[0] || loaded.ExpoTokensByOwner[1] != cfg.ExpoTokensByOwner[1] {
		t.Errorf("ExpoTokensByOwner = %+v, want %+v", loaded.ExpoTokensByOwner, cfg.ExpoTokensByOwner)
	}
}

func TestSavePreservesAnExplicitlySetMasterKey(t *testing.T) {
	withXDGConfigHome(t, t.TempDir())
	cfg := New()
	cfg.MasterKey = "already-set-master-key"
	if err := Save(&cfg); err != nil {
		t.Fatalf("Save: %v", err)
	}
	if cfg.MasterKey != "already-set-master-key" {
		t.Errorf("Save must not overwrite an already-set MasterKey, got %q", cfg.MasterKey)
	}
}

func TestExpoTokenForPrefersOwnerMatchOverDefault(t *testing.T) {
	cfg := New()
	cfg.ExpoToken = "default-token"
	cfg.ExpoTokensByOwner = []ExpoTokenEntry{{Owner: "project-cell", Token: "owner-token"}}

	if got := cfg.ExpoTokenFor("project-cell"); got != "owner-token" {
		t.Errorf("ExpoTokenFor(match) = %q, want %q", got, "owner-token")
	}
	if got := cfg.ExpoTokenFor("some-other-owner"); got != "default-token" {
		t.Errorf("ExpoTokenFor(no match) = %q, want %q", got, "default-token")
	}
	if got := cfg.ExpoTokenFor(""); got != "default-token" {
		t.Errorf("ExpoTokenFor(empty) = %q, want %q", got, "default-token")
	}
}

func TestCorruptMachineKeyIsRegeneratedRatherThanFailingForever(t *testing.T) {
	dir := t.TempDir()
	withXDGConfigHome(t, dir)

	eblDir := filepath.Join(dir, "ebl")
	if err := os.MkdirAll(eblDir, 0o700); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(eblDir, "machine.key"), []byte("not a valid key"), 0o600); err != nil {
		t.Fatal(err)
	}

	cfg := New()
	cfg.ExpoToken = "token-after-key-regen"
	if err := Save(&cfg); err != nil {
		t.Fatalf("Save should recover from a corrupt machine key, got: %v", err)
	}

	loaded, found, err := Load()
	if err != nil {
		t.Fatalf("Load: %v", err)
	}
	if !found || loaded.ExpoToken != "token-after-key-regen" {
		t.Errorf("loaded = %+v, found = %v", loaded, found)
	}
}

// TestCrossCompatibleWithCppImplementation locks in wire/file compatibility
// with cli/src/config_store.cpp - the testdata fixture (config.json +
// machine.key) was produced by the real C++ implementation and verified by
// hand to round-trip correctly in both directions (C++ writes / Go reads, and
// Go writes / C++ reads) before this test was written. A failure here means
// an existing user's ~/.config/ebl/config.json would stop working after
// upgrading from the C++ CLI to this one.
func TestCrossCompatibleWithCppImplementation(t *testing.T) {
	fixtureDir, err := filepath.Abs("testdata/xdg_fixture")
	if err != nil {
		t.Fatal(err)
	}
	withXDGConfigHome(t, fixtureDir)

	cfg, found, err := Load()
	if err != nil {
		t.Fatalf("Load: %v", err)
	}
	if !found {
		t.Fatal("expected found = true")
	}
	if cfg.ProjectsRoot != "/home/dev/projects" {
		t.Errorf("ProjectsRoot = %q", cfg.ProjectsRoot)
	}
	if cfg.OrchestratorPort != 4001 || cfg.WebPort != 3000 {
		t.Errorf("OrchestratorPort/WebPort = %d/%d", cfg.OrchestratorPort, cfg.WebPort)
	}
	if cfg.ExpoToken != "default-token-abc" {
		t.Errorf("ExpoToken = %q", cfg.ExpoToken)
	}
	if cfg.MasterKey != "QokyE8eGsSIUyysvrlrI5F95VkFS4EwMeT0pQxga5hA=" {
		t.Errorf("MasterKey = %q", cfg.MasterKey)
	}
	if cfg.SetupCompletedAt != 1234567890 {
		t.Errorf("SetupCompletedAt = %d", cfg.SetupCompletedAt)
	}
	if len(cfg.ExpoTokensByOwner) != 1 || cfg.ExpoTokensByOwner[0].Owner != "project-cell" ||
		cfg.ExpoTokensByOwner[0].Token != "owner-token-xyz" {
		t.Errorf("ExpoTokensByOwner = %+v", cfg.ExpoTokensByOwner)
	}
}
