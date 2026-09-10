package metrics

import (
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

func TestExtractReadsVersionApplicationIdAndHash(t *testing.T) {
	dir := t.TempDir()
	androidAppDir := filepath.Join(dir, "android", "app")
	if err := os.MkdirAll(androidAppDir, 0o755); err != nil {
		t.Fatal(err)
	}
	buildGradle := `
android {
    defaultConfig {
        applicationId "com.example.myapp"
        versionCode 42
        versionName "1.2.3"
    }
}
`
	if err := os.WriteFile(filepath.Join(androidAppDir, "build.gradle"), []byte(buildGradle), 0o644); err != nil {
		t.Fatal(err)
	}

	artifactPath := filepath.Join(dir, "app.apk")
	content := []byte("fake apk bytes for hashing")
	if err := os.WriteFile(artifactPath, content, 0o644); err != nil {
		t.Fatal(err)
	}

	m, err := Extract(dir, artifactPath)
	if err != nil {
		t.Fatalf("Extract: %v", err)
	}
	if m.ApplicationID != "com.example.myapp" {
		t.Errorf("ApplicationID = %q", m.ApplicationID)
	}
	if m.VersionCode != "42" {
		t.Errorf("VersionCode = %q", m.VersionCode)
	}
	if m.VersionName != "1.2.3" {
		t.Errorf("VersionName = %q", m.VersionName)
	}
	if m.SizeBytes != uint64(len(content)) {
		t.Errorf("SizeBytes = %d, want %d", m.SizeBytes, len(content))
	}
	// sha256("fake apk bytes for hashing")
	if m.SHA256 == "" || len(m.SHA256) != 64 {
		t.Errorf("SHA256 = %q, want a 64-hex-char digest", m.SHA256)
	}
}

func TestExtractFallsBackToPackageJsonVersionWhenGradleHasNone(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "package.json"), []byte(`{"name":"myapp","version":"9.9.9"}`), 0o644); err != nil {
		t.Fatal(err)
	}
	artifactPath := filepath.Join(dir, "app.apk")
	if err := os.WriteFile(artifactPath, []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}

	m, err := Extract(dir, artifactPath)
	if err != nil {
		t.Fatalf("Extract: %v", err)
	}
	if m.VersionName != "9.9.9" {
		t.Errorf("VersionName = %q, want 9.9.9", m.VersionName)
	}
}

func TestExtractErrorsOnMissingArtifact(t *testing.T) {
	dir := t.TempDir()
	if _, err := Extract(dir, filepath.Join(dir, "does-not-exist.apk")); err == nil {
		t.Error("expected an error for a missing artifact")
	}
}

func TestExtractGitMetadataFromARealRepo(t *testing.T) {
	if _, err := exec.LookPath("git"); err != nil {
		t.Skip("git not available")
	}
	dir := t.TempDir()
	run := func(args ...string) {
		cmd := exec.Command("git", args...)
		cmd.Dir = dir
		if out, err := cmd.CombinedOutput(); err != nil {
			t.Fatalf("git %v: %v\n%s", args, err, out)
		}
	}
	run("init", "-q", "-b", "main")
	run("config", "user.email", "test@example.com")
	run("config", "user.name", "Test")
	if err := os.WriteFile(filepath.Join(dir, "README.md"), []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}
	run("add", "README.md")
	run("commit", "-q", "-m", "initial")

	artifactPath := filepath.Join(dir, "app.apk")
	if err := os.WriteFile(artifactPath, []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}

	m, err := Extract(dir, artifactPath)
	if err != nil {
		t.Fatalf("Extract: %v", err)
	}
	if m.GitBranch != "main" {
		t.Errorf("GitBranch = %q, want main", m.GitBranch)
	}
	if len(m.GitCommit) < 4 {
		t.Errorf("GitCommit = %q, want a short hash", m.GitCommit)
	}
}
