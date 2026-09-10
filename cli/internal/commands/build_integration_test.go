package commands

import (
	"context"
	"encoding/json"
	"os"
	"os/exec"
	"path/filepath"
	"testing"

	"github.com/41vi4p/expo-builder-local/cli/internal/dockerapi"
)

// fakeRunnerImage is built from ./testdata/fakerunner - a tiny alpine-based
// image whose entrypoint emits exactly the same @@PHASE:/@@PROGRESS:/
// @@ENGINE:/@@BUILD_NUMBER:/@@ARTIFACT: marker protocol
// docker/runner/build-entrypoint.sh does (see that script's own header
// comment), instantly instead of running a real multi-minute Android build.
// This lets RunBuild's actual Docker orchestration (container create/
// attach/wait/stats-poll, marker parsing, progress tracking, artifact-path
// translation, metrics extraction) be exercised end to end against a real
// daemon without needing the real several-GB runner image.
const fakeRunnerImage = "ebl-go-fake-runner:test"

func requireDockerAndFakeRunner(t *testing.T) {
	t.Helper()
	docker := dockerapi.New("/var/run/docker.sock")
	if !docker.Ping(context.Background()) {
		t.Skip("no Docker daemon reachable - skipping build integration test")
	}
	if os := docker.DaemonOS(context.Background()); os != "" && os != "linux" {
		t.Skipf("Docker daemon is in %s-container mode - this test needs Linux images", os)
	}
	if exists, err := docker.ImageExists(context.Background(), fakeRunnerImage); err != nil || !exists {
		build := exec.Command("docker", "build", "-t", fakeRunnerImage, "./testdata/fakerunner")
		if out, err := build.CombinedOutput(); err != nil {
			t.Fatalf("building fake runner image: %v\n%s", err, out)
		}
	}
}

// makeFakeExpoProject creates a minimal but real-enough Expo project
// directory: package.json with an expo dependency (so detect.ExpoProject
// succeeds) and android/app/build.gradle (so metrics.Extract has real
// values to find).
func makeFakeExpoProject(t *testing.T) string {
	t.Helper()
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "package.json"),
		[]byte(`{"name":"fake-app","version":"1.0.0","dependencies":{"expo":"^52.0.0"}}`), 0o644); err != nil {
		t.Fatal(err)
	}
	androidAppDir := filepath.Join(dir, "android", "app")
	if err := os.MkdirAll(androidAppDir, 0o755); err != nil {
		t.Fatal(err)
	}
	buildGradle := `android {
    defaultConfig {
        applicationId "com.example.fakeapp"
        versionCode 7
        versionName "1.0.0"
    }
}
`
	if err := os.WriteFile(filepath.Join(androidAppDir, "build.gradle"), []byte(buildGradle), 0o644); err != nil {
		t.Fatal(err)
	}
	return dir
}

func TestRunBuildEndToEndAgainstRealDockerJSON(t *testing.T) {
	requireDockerAndFakeRunner(t)
	projectDir := makeFakeExpoProject(t)

	out := captureStdoutStderr(t, func() {
		code := RunBuild([]string{
			projectDir,
			"--runner-image", fakeRunnerImage,
			"--engine", "gradle",
			"--gradle-cache-volume", "ebl_go_test_gradle_cache",
			"--npm-cache-volume", "ebl_go_test_npm_cache",
			"--json",
		})
		if code != 0 {
			t.Errorf("RunBuild exit code = %d, want 0", code)
		}
	})

	var result struct {
		Success       bool    `json:"success"`
		ArtifactPath  string  `json:"artifactPath"`
		SizeBytes     float64 `json:"sizeBytes"`
		VersionName   string  `json:"versionName"`
		VersionCode   string  `json:"versionCode"`
		ApplicationID string  `json:"applicationId"`
		Engine        string  `json:"engine"`
		BuildNumber   string  `json:"buildNumber"`
		SHA256        string  `json:"sha256"`
	}
	if err := json.Unmarshal([]byte(out.stdout), &result); err != nil {
		t.Fatalf("parsing --json output: %v\nstdout: %q\nstderr: %q", err, out.stdout, out.stderr)
	}

	if !result.Success {
		t.Fatalf("success = false, stdout: %q, stderr: %q", out.stdout, out.stderr)
	}
	wantArtifact := filepath.Join(projectDir, "ebl_builds", "v1.0.0-build1", "app.apk")
	if result.ArtifactPath != wantArtifact {
		t.Errorf("ArtifactPath = %q, want %q", result.ArtifactPath, wantArtifact)
	}
	if _, err := os.Stat(result.ArtifactPath); err != nil {
		t.Errorf("expected the artifact to actually exist on the host at %q: %v", result.ArtifactPath, err)
	}
	if result.VersionName != "1.0.0" || result.VersionCode != "7" || result.ApplicationID != "com.example.fakeapp" {
		t.Errorf("metrics not extracted correctly: %+v", result)
	}
	if result.Engine != "gradle" {
		t.Errorf("Engine = %q, want gradle (from @@ENGINE:)", result.Engine)
	}
	if result.BuildNumber != "1" {
		t.Errorf("BuildNumber = %q, want 1", result.BuildNumber)
	}
	if result.SHA256 == "" {
		t.Error("expected a non-empty SHA256")
	}
}

func TestRunBuildEndToEndAgainstRealDockerLogs(t *testing.T) {
	requireDockerAndFakeRunner(t)
	projectDir := makeFakeExpoProject(t)

	out := captureStdoutStderr(t, func() {
		code := RunBuild([]string{
			projectDir,
			"--runner-image", fakeRunnerImage,
			"--engine", "gradle",
			"--gradle-cache-volume", "ebl_go_test_gradle_cache",
			"--npm-cache-volume", "ebl_go_test_npm_cache",
			"--logs",
		})
		if code != 0 {
			t.Errorf("RunBuild exit code = %d, want 0", code)
		}
	})

	// --logs mode: marker lines must be suppressed, but the raw build
	// output should stream through, and the human-readable success summary
	// should appear.
	if contains(out.stdout, "@@PHASE:") || contains(out.stdout, "@@PROGRESS:") {
		t.Errorf("marker lines leaked into --logs output: %q", out.stdout)
	}
	if !contains(out.stdout, "EXECUTING") {
		t.Errorf("expected the raw Gradle-style progress line to stream through in --logs mode, got: %q", out.stdout)
	}
	if !contains(out.stdout, "succeeded") {
		t.Errorf("expected a success summary, got: %q", out.stdout)
	}
}

type capturedOutput struct{ stdout, stderr string }

func contains(s, substr string) bool {
	return len(s) >= len(substr) && (func() bool {
		for i := 0; i+len(substr) <= len(s); i++ {
			if s[i:i+len(substr)] == substr {
				return true
			}
		}
		return false
	})()
}

// captureStdoutStderr redirects both os.Stdout and os.Stderr for the
// duration of fn, returning everything written to each.
func captureStdoutStderr(t *testing.T, fn func()) capturedOutput {
	t.Helper()
	outR, outW, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	errR, errW, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	oldOut, oldErr := os.Stdout, os.Stderr
	os.Stdout, os.Stderr = outW, errW

	fn()

	outW.Close()
	errW.Close()
	os.Stdout, os.Stderr = oldOut, oldErr

	outBuf := make([]byte, 1<<20)
	n, _ := outR.Read(outBuf)
	errBuf := make([]byte, 1<<20)
	m, _ := errR.Read(errBuf)
	return capturedOutput{stdout: string(outBuf[:n]), stderr: string(errBuf[:m])}
}
