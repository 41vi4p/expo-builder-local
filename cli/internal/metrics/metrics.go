// Package metrics computes post-build artifact metrics: size, SHA-256,
// version/application ID pulled from the Gradle project that actually
// produced the artifact, and git commit/branch. Runs entirely on the host
// filesystem (the CLI executes natively, not in a container). Direct port
// of cli/src/metrics.hpp/.cpp.
package metrics

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
)

// ArtifactMetrics is everything extracted about one build's output artifact.
type ArtifactMetrics struct {
	SizeBytes     uint64
	SHA256        string
	VersionName   string
	VersionCode   string
	ApplicationID string
	GitCommit     string
	GitBranch     string
}

func sha256File(path string) (string, error) {
	f, err := os.Open(path)
	if err != nil {
		return "", fmt.Errorf("cannot open artifact for hashing: %s", path)
	}
	defer f.Close()

	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return "", err
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}

// runCommandCapture runs a command with argv directly (no shell involved)
// and returns its trimmed stdout, or an empty string if it exits non-zero
// or can't be spawned. Used only for git, which is optional metadata -
// never fatal to the build if unavailable. os/exec already handles
// cross-platform argv passing and quoting correctly, replacing the C++
// version's separate fork+execvp/CreateProcess branches entirely.
func runCommandCapture(args ...string) string {
	if len(args) == 0 {
		return ""
	}
	cmd := exec.Command(args[0], args[1:]...)
	out, err := cmd.Output()
	if err != nil {
		return ""
	}
	return strings.TrimRight(string(out), "\r\n")
}

var (
	applicationIDRE  = regexp.MustCompile(`applicationId\s+["']([^"']+)["']`)
	versionCodeRE    = regexp.MustCompile(`versionCode\s+(\d+)`)
	versionNameRE    = regexp.MustCompile(`versionName\s+["']([^"']+)["']`)
	packageVersionRE = regexp.MustCompile(`"version"\s*:\s*"([^"]+)"`)
)

type gradleValues struct {
	applicationID string
	versionCode   string
	versionName   string
}

func readGradleManifestValues(appPath string) gradleValues {
	var values gradleValues
	src, err := os.ReadFile(filepath.Join(appPath, "android", "app", "build.gradle"))
	if err != nil {
		return values
	}
	if m := applicationIDRE.FindSubmatch(src); m != nil {
		values.applicationID = string(m[1])
	}
	if m := versionCodeRE.FindSubmatch(src); m != nil {
		values.versionCode = string(m[1])
	}
	if m := versionNameRE.FindSubmatch(src); m != nil {
		values.versionName = string(m[1])
	}
	return values
}

func readPackageJSONVersion(appPath string) string {
	src, err := os.ReadFile(filepath.Join(appPath, "package.json"))
	if err != nil {
		return ""
	}
	if m := packageVersionRE.FindSubmatch(src); m != nil {
		return string(m[1])
	}
	return ""
}

// Extract computes ArtifactMetrics for the artifact at artifactPath,
// produced by the Expo project at appPath.
func Extract(appPath, artifactPath string) (ArtifactMetrics, error) {
	var metrics ArtifactMetrics

	info, err := os.Stat(artifactPath)
	if err != nil {
		return ArtifactMetrics{}, fmt.Errorf("artifact not found at %s", artifactPath)
	}
	metrics.SizeBytes = uint64(info.Size())

	metrics.SHA256, err = sha256File(artifactPath)
	if err != nil {
		return ArtifactMetrics{}, err
	}

	gradle := readGradleManifestValues(appPath)
	metrics.ApplicationID = gradle.applicationID
	metrics.VersionCode = gradle.versionCode
	if gradle.versionName != "" {
		metrics.VersionName = gradle.versionName
	} else {
		metrics.VersionName = readPackageJSONVersion(appPath)
	}

	metrics.GitCommit = runCommandCapture("git", "-C", appPath, "rev-parse", "--short", "HEAD")
	metrics.GitBranch = runCommandCapture("git", "-C", appPath, "rev-parse", "--abbrev-ref", "HEAD")

	return metrics, nil
}
