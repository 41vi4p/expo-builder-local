// Package config reads/writes the CLI's local persisted configuration - the
// result of `ebl config` - at ~/.config/ebl/config.json (0600, %APPDATA%\ebl on
// Windows). The Expo token and the orchestrator's generated MASTER_KEY are
// encrypted at rest via cryptoutil, using a machine-local key at
// ~/.config/ebl/machine.key (0600, generated on first use). Neither file is
// ever meant to leave this machine. Direct port of cli/src/config_store.cpp.
package config

import (
	"encoding/base64"
	"encoding/json"
	"errors"
	"os"
	"os/user"
	"path/filepath"
	"runtime"

	"github.com/41vi4p/expo-builder-local/cli/internal/cryptoutil"
)

// ExpoTokenEntry is a saved token for one EAS account (app.json's
// expo.owner slug, e.g. "project-cell"). Owner is never empty here - the
// single unscoped fallback lives in Config.ExpoToken instead, so there's
// exactly one place a "no owner matched" token comes from.
type ExpoTokenEntry struct {
	Owner string
	Token string // plaintext once loaded into memory; encrypted on disk
}

// Config is the CLI's persisted configuration, decrypted into memory.
type Config struct {
	ProjectsRoot      string
	OrchestratorPort  int
	WebPort           int
	MasterKey         string // plaintext once loaded into memory; encrypted on disk
	ExpoToken         string // default/fallback token, used when no owner-specific entry matches (may be empty)
	ExpoTokensByOwner []ExpoTokenEntry
	SetupCompletedAt  int64 // 0 = setup has never completed
}

// New returns a Config with the documented defaults - the equivalent of the
// C++ EblConfig struct's default member initializers.
func New() Config {
	return Config{OrchestratorPort: 4001, WebPort: 3000}
}

// All three always come from the canonical upstream account - this used to be
// configurable (a "Docker Hub namespace" field/prompt in `ebl config`), but
// that was removed: nobody actually needs to point this at a different
// account, and it just added an unused prompt to the setup wizard.
func (Config) RunnerImage() string       { return "41vi4p/expo-builder-local-runner:latest" }
func (Config) OrchestratorImage() string { return "41vi4p/expo-builder-local-orchestrator:latest" }
func (Config) WebImage() string          { return "41vi4p/expo-builder-local-web:latest" }

// ExpoTokenFor resolves the token to use for a project whose app.json
// declares owner (empty string if it doesn't declare one) - an exact owner
// match wins, otherwise falls back to the single default ExpoToken.
func (c Config) ExpoTokenFor(owner string) string {
	if owner != "" {
		for _, entry := range c.ExpoTokensByOwner {
			if entry.Owner == owner {
				return entry.Token
			}
		}
	}
	return c.ExpoToken
}

func homeDir() (string, error) {
	if h := os.Getenv("HOME"); h != "" {
		return h, nil
	}
	if u, err := user.Current(); err == nil && u.HomeDir != "" {
		return u.HomeDir, nil
	}
	return "", errors.New("could not determine home directory (HOME is unset)")
}

// Dir returns the config directory - $XDG_CONFIG_HOME/ebl if set, else
// %APPDATA%\ebl on Windows, else ~/.config/ebl on POSIX (Linux and macOS
// alike - deliberately not macOS's usual ~/Library/Application Support, to
// match this project's established, documented convention).
func Dir() (string, error) {
	if xdg := os.Getenv("XDG_CONFIG_HOME"); xdg != "" {
		return filepath.Join(xdg, "ebl"), nil
	}
	if runtime.GOOS == "windows" {
		appData := os.Getenv("APPDATA")
		if appData == "" {
			return "", errors.New("could not determine a config directory (%APPDATA% is unset)")
		}
		return filepath.Join(appData, "ebl"), nil
	}
	home, err := homeDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(home, ".config", "ebl"), nil
}

// FilePath returns the full path to config.json.
func FilePath() (string, error) {
	dir, err := Dir()
	if err != nil {
		return "", err
	}
	return filepath.Join(dir, "config.json"), nil
}

func machineKeyPath() (string, error) {
	dir, err := Dir()
	if err != nil {
		return "", err
	}
	return filepath.Join(dir, "machine.key"), nil
}

func loadOrCreateMachineKey() (cryptoutil.Key, error) {
	path, err := machineKeyPath()
	if err != nil {
		return cryptoutil.Key{}, err
	}

	if data, err := os.ReadFile(path); err == nil {
		if key, err := decodeMachineKey(data); err == nil {
			return key, nil
		}
		// Fall through and regenerate if the file is corrupt/wrong size -
		// better than hard-failing every command forever because of one bad
		// write.
	}

	key, err := cryptoutil.GenerateKey()
	if err != nil {
		return cryptoutil.Key{}, err
	}
	if err := os.WriteFile(path, []byte(base64Key(key)), 0o600); err != nil {
		return cryptoutil.Key{}, err
	}
	// On Windows, secret files rely on per-user profile isolation (%APPDATA%
	// is already private to the owning account) rather than an explicit ACL
	// tighten - os.WriteFile's mode argument is a no-op there anyway.
	return key, nil
}

type onDiskTokenEntry struct {
	Owner    string `json:"owner"`
	TokenEnc string `json:"tokenEnc"`
}

// onDiskConfig mirrors the JSON file layout exactly - kept separate from the
// in-memory Config so encrypted-vs-plaintext fields can't be mixed up.
// OrchestratorPort/WebPort are pointers so a missing key is distinguishable
// from an explicit 0 (matches the C++ version's Json::asInt(default) pattern,
// where the default is only used when the key is absent or non-numeric).
type onDiskConfig struct {
	ProjectsRoot      string             `json:"projectsRoot"`
	OrchestratorPort  *int               `json:"orchestratorPort,omitempty"`
	WebPort           *int               `json:"webPort,omitempty"`
	SetupCompletedAt  int64              `json:"setupCompletedAt"`
	MasterKeyEnc      string             `json:"masterKeyEnc,omitempty"`
	ExpoTokenEnc      string             `json:"expoTokenEnc,omitempty"`
	ExpoTokensByOwner []onDiskTokenEntry `json:"expoTokensByOwner"`
}

// Load reads and decrypts the saved config. Returns (Config{}, false, nil) if
// no config has been saved yet (i.e. `ebl config` was never run).
func Load() (Config, bool, error) {
	path, err := FilePath()
	if err != nil {
		return Config{}, false, err
	}
	text, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			return Config{}, false, nil
		}
		return Config{}, false, err
	}
	if len(text) == 0 {
		return Config{}, false, nil
	}

	var disk onDiskConfig
	if err := json.Unmarshal(text, &disk); err != nil {
		return Config{}, false, err
	}

	cfg := New()
	cfg.ProjectsRoot = disk.ProjectsRoot
	if disk.OrchestratorPort != nil {
		cfg.OrchestratorPort = *disk.OrchestratorPort
	}
	if disk.WebPort != nil {
		cfg.WebPort = *disk.WebPort
	}
	cfg.SetupCompletedAt = disk.SetupCompletedAt

	key, err := loadOrCreateMachineKey()
	if err != nil {
		return Config{}, false, err
	}
	if disk.MasterKeyEnc != "" {
		if cfg.MasterKey, err = cryptoutil.Decrypt(disk.MasterKeyEnc, key); err != nil {
			return Config{}, false, err
		}
	}
	if disk.ExpoTokenEnc != "" {
		if cfg.ExpoToken, err = cryptoutil.Decrypt(disk.ExpoTokenEnc, key); err != nil {
			return Config{}, false, err
		}
	}
	for _, entry := range disk.ExpoTokensByOwner {
		if entry.Owner == "" || entry.TokenEnc == "" {
			continue
		}
		token, err := cryptoutil.Decrypt(entry.TokenEnc, key)
		if err != nil {
			return Config{}, false, err
		}
		cfg.ExpoTokensByOwner = append(cfg.ExpoTokensByOwner, ExpoTokenEntry{Owner: entry.Owner, Token: token})
	}

	return cfg, true, nil
}

// Save encrypts and writes the config file (and machine key, if this is the
// first save) with 0600 permissions. Generates a MasterKey automatically if
// cfg.MasterKey is still empty, mutating cfg in place - same signature
// contract as the C++ version's saveConfig(EblConfig&).
func Save(cfg *Config) error {
	dir, err := Dir()
	if err != nil {
		return err
	}
	// 0700: matches S_IRWXU on POSIX; a no-op mode on Windows, which relies on
	// per-user profile isolation instead (same reasoning as the machine key).
	if err := os.MkdirAll(dir, 0o700); err != nil {
		return err
	}

	if cfg.MasterKey == "" {
		generated, err := cryptoutil.GenerateKey()
		if err != nil {
			return err
		}
		cfg.MasterKey = base64Key(generated)
	}

	key, err := loadOrCreateMachineKey()
	if err != nil {
		return err
	}

	disk := onDiskConfig{
		ProjectsRoot:     cfg.ProjectsRoot,
		OrchestratorPort: &cfg.OrchestratorPort,
		WebPort:          &cfg.WebPort,
		SetupCompletedAt: cfg.SetupCompletedAt,
	}
	if disk.MasterKeyEnc, err = cryptoutil.Encrypt(cfg.MasterKey, key); err != nil {
		return err
	}
	if cfg.ExpoToken != "" {
		if disk.ExpoTokenEnc, err = cryptoutil.Encrypt(cfg.ExpoToken, key); err != nil {
			return err
		}
	}
	disk.ExpoTokensByOwner = []onDiskTokenEntry{}
	for _, entry := range cfg.ExpoTokensByOwner {
		if entry.Owner == "" || entry.Token == "" {
			continue
		}
		tokenEnc, err := cryptoutil.Encrypt(entry.Token, key)
		if err != nil {
			return err
		}
		disk.ExpoTokensByOwner = append(disk.ExpoTokensByOwner, onDiskTokenEntry{Owner: entry.Owner, TokenEnc: tokenEnc})
	}

	serialized, err := json.Marshal(disk)
	if err != nil {
		return err
	}

	// Write to a sibling temp file and rename() over the real path (atomic on
	// the same filesystem - os.Rename already does MOVEFILE_REPLACE_EXISTING
	// on Windows internally), rather than truncating the real file in place -
	// a crash, kill, or ENOSPC partway through an in-place write leaves a
	// corrupt/empty config.json; a rename can only ever land the old file or
	// the fully-written new one, never a partial.
	path, err := FilePath()
	if err != nil {
		return err
	}
	tmpPath := path + ".tmp"
	if err := os.WriteFile(tmpPath, serialized, 0o600); err != nil {
		return err
	}
	return os.Rename(tmpPath, path)
}

func base64Key(key cryptoutil.Key) string {
	// base64, matching the C++ version's plain base64-of-raw-32-bytes format.
	return base64.StdEncoding.EncodeToString(key[:])
}

func decodeMachineKey(data []byte) (cryptoutil.Key, error) {
	decoded, err := base64.StdEncoding.DecodeString(string(data))
	if err != nil {
		return cryptoutil.Key{}, err
	}
	var zero cryptoutil.Key
	if len(decoded) != len(zero) {
		return cryptoutil.Key{}, errors.New("machine key file has the wrong size")
	}
	var key cryptoutil.Key
	copy(key[:], decoded)
	return key, nil
}
