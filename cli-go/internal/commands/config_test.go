package commands

import (
	"strings"
	"testing"

	"github.com/41vi4p/expo-builder-local/cli/internal/config"
	"github.com/41vi4p/expo-builder-local/cli/internal/prompt"
)

func TestConfigHelpReturnsZero(t *testing.T) {
	silenceOutput(t)
	if code := RunConfig([]string{"--help"}); code != 0 {
		t.Errorf("code = %d, want 0", code)
	}
}

func TestConfigInteractiveWizardEndToEnd(t *testing.T) {
	silenceOutput(t)
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	projectsDir := t.TempDir()

	// Sequence: projects folder, default token, add owner "acct-a" with a
	// token, finish adding accounts, orchestrator port, web port.
	input := projectsDir + "\n" + // projects folder
		"a-default-token\n" + // default Expo token
		"acct-a\n" + // add owner
		"token-for-a\n" + // its token
		"\n" + // finish adding accounts
		"5001\n" + // orchestrator port
		"6001\n" // web port
	restore := prompt.SetInputForTesting(strings.NewReader(input))
	defer restore()

	if code := RunConfig(nil); code != 0 {
		t.Fatalf("code = %d, want 0", code)
	}

	cfg, found, err := config.Load()
	if err != nil {
		t.Fatalf("Load: %v", err)
	}
	if !found {
		t.Fatal("expected a saved config")
	}
	if cfg.ProjectsRoot != projectsDir {
		t.Errorf("ProjectsRoot = %q, want %q", cfg.ProjectsRoot, projectsDir)
	}
	if cfg.ExpoToken != "a-default-token" {
		t.Errorf("ExpoToken = %q", cfg.ExpoToken)
	}
	if len(cfg.ExpoTokensByOwner) != 1 || cfg.ExpoTokensByOwner[0].Owner != "acct-a" || cfg.ExpoTokensByOwner[0].Token != "token-for-a" {
		t.Errorf("ExpoTokensByOwner = %+v", cfg.ExpoTokensByOwner)
	}
	if cfg.OrchestratorPort != 5001 {
		t.Errorf("OrchestratorPort = %d, want 5001", cfg.OrchestratorPort)
	}
	if cfg.WebPort != 6001 {
		t.Errorf("WebPort = %d, want 6001", cfg.WebPort)
	}
}

func TestConfigRejectsNonexistentProjectsFolderAndReprompts(t *testing.T) {
	silenceOutput(t)
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	goodDir := t.TempDir()

	input := "/definitely/not/a/real/directory-xyz\n" + // rejected, re-prompted
		goodDir + "\n" + // accepted
		"\n" + // no default token
		"\n" + // no owner accounts
		"4001\n" + // default orchestrator port
		"3000\n" // default web port
	restore := prompt.SetInputForTesting(strings.NewReader(input))
	defer restore()

	if code := RunConfig(nil); code != 0 {
		t.Fatalf("code = %d, want 0", code)
	}

	cfg, found, err := config.Load()
	if err != nil || !found {
		t.Fatalf("Load: found=%v err=%v", found, err)
	}
	if cfg.ProjectsRoot != goodDir {
		t.Errorf("ProjectsRoot = %q, want %q", cfg.ProjectsRoot, goodDir)
	}
}

func TestConfigClearsDefaultTokenOnClearKeyword(t *testing.T) {
	silenceOutput(t)
	dir := t.TempDir()
	t.Setenv("XDG_CONFIG_HOME", dir)
	projectsDir := t.TempDir()

	// First run: set a default token.
	restore := prompt.SetInputForTesting(strings.NewReader(projectsDir + "\n" + "some-token\n" + "\n" + "4001\n" + "3000\n"))
	if code := RunConfig(nil); code != 0 {
		restore()
		t.Fatalf("first run: code = %d, want 0", code)
	}
	restore()

	cfg, _, err := config.Load()
	if err != nil {
		t.Fatalf("Load: %v", err)
	}
	if cfg.ExpoToken != "some-token" {
		t.Fatalf("precondition failed: ExpoToken = %q", cfg.ExpoToken)
	}

	// Second run: clear it.
	restore = prompt.SetInputForTesting(strings.NewReader(projectsDir + "\n" + "clear\n" + "\n" + "4001\n" + "3000\n"))
	defer restore()
	if code := RunConfig(nil); code != 0 {
		t.Fatalf("second run: code = %d, want 0", code)
	}

	cfg, _, err = config.Load()
	if err != nil {
		t.Fatalf("Load: %v", err)
	}
	if cfg.ExpoToken != "" {
		t.Errorf("ExpoToken = %q, want empty after clearing", cfg.ExpoToken)
	}
}

func TestMaskedPreview(t *testing.T) {
	cases := map[string]string{
		"":         "(not set)",
		"abcd":     "****",
		"abcdefgh": "****efgh",
	}
	for in, want := range cases {
		if got := maskedPreview(in); got != want {
			t.Errorf("maskedPreview(%q) = %q, want %q", in, got, want)
		}
	}
}
