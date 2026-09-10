package commands

import (
	"os"
	"path/filepath"
	"testing"
)

func TestParseBuildArgsDefaults(t *testing.T) {
	opts, code, ok := parseBuildArgs(nil)
	if !ok || code != 0 {
		t.Fatalf("ok=%v code=%d", ok, code)
	}
	if opts.path != "." || opts.engine != "eas" || opts.gradleCacheVolume != "expo-builder-local_gradle-cache" ||
		opts.npmCacheVolume != "expo-builder-local_npm-cache" || opts.dockerSocket != "/var/run/docker.sock" {
		t.Errorf("unexpected defaults: %+v", opts)
	}
}

func TestParseBuildArgsHelp(t *testing.T) {
	silenceOutput(t)
	_, code, ok := parseBuildArgs([]string{"--help"})
	if ok || code != 0 {
		t.Errorf("ok=%v code=%d, want ok=false code=0", ok, code)
	}
}

func TestParseBuildArgsMultipleFlagsTogether(t *testing.T) {
	// Regression coverage for the "does ebl build . --status --prod work"
	// question this session's C++ work confirmed was never actually broken
	// (parseArgs is a flat loop with independent per-flag branches) - same
	// property must hold for the Go port.
	opts, code, ok := parseBuildArgs([]string{".", "--status", "--prod", "-a", "aab", "--engine", "gradle"})
	if !ok || code != 0 {
		t.Fatalf("ok=%v code=%d", ok, code)
	}
	if !opts.prod || opts.artifact != "aab" || opts.engine != "gradle" {
		t.Errorf("unexpected opts: %+v", opts)
	}
}

func TestParseBuildArgsUnknownOption(t *testing.T) {
	silenceOutput(t)
	_, code, ok := parseBuildArgs([]string{"--bogus"})
	if ok || code != 2 {
		t.Errorf("ok=%v code=%d, want ok=false code=2", ok, code)
	}
}

func TestParseBuildArgsMissingFlagValue(t *testing.T) {
	silenceOutput(t)
	_, code, ok := parseBuildArgs([]string{"--artifact"})
	if ok || code != 2 {
		t.Errorf("ok=%v code=%d, want ok=false code=2", ok, code)
	}
}

func TestParseBuildArgsExtraPositionalArgument(t *testing.T) {
	silenceOutput(t)
	_, code, ok := parseBuildArgs([]string{"path1", "path2"})
	if ok || code != 2 {
		t.Errorf("ok=%v code=%d, want ok=false code=2", ok, code)
	}
}

func TestParseBuildArgsStatusFlagIsASilentNoOp(t *testing.T) {
	opts, code, ok := parseBuildArgs([]string{"--status"})
	if !ok || code != 0 {
		t.Fatalf("ok=%v code=%d", ok, code)
	}
	_ = opts // --status sets nothing; just confirm it doesn't error
}

func TestRunBuildTuiAndJsonConflict(t *testing.T) {
	silenceOutput(t)
	code := RunBuild([]string{"--tui", "--json"})
	if code != 2 {
		t.Errorf("code = %d, want 2", code)
	}
}

func TestRunBuildRejectsInvalidArtifact(t *testing.T) {
	silenceOutput(t)
	dir := t.TempDir()
	writeExpoPackageJSON(t, dir)
	code := RunBuild([]string{dir, "--artifact", "exe"})
	if code != 2 {
		t.Errorf("code = %d, want 2", code)
	}
}

func TestRunBuildRejectsInvalidEngine(t *testing.T) {
	silenceOutput(t)
	dir := t.TempDir()
	writeExpoPackageJSON(t, dir)
	code := RunBuild([]string{dir, "--engine", "xcode"})
	if code != 2 {
		t.Errorf("code = %d, want 2", code)
	}
}

func TestRunBuildRejectsReleaseWithoutKeystore(t *testing.T) {
	silenceOutput(t)
	dir := t.TempDir()
	writeExpoPackageJSON(t, dir)
	code := RunBuild([]string{dir, "--release"})
	if code != 2 {
		t.Errorf("code = %d, want 2", code)
	}
}

func TestRunBuildRejectsMissingKeystoreFile(t *testing.T) {
	silenceOutput(t)
	dir := t.TempDir()
	writeExpoPackageJSON(t, dir)
	code := RunBuild([]string{dir, "--release", "--keystore", filepath.Join(dir, "does-not-exist.jks")})
	if code != 2 {
		t.Errorf("code = %d, want 2", code)
	}
}

func TestRunBuildRejectsNonExpoProject(t *testing.T) {
	silenceOutput(t)
	dir := t.TempDir() // no package.json at all
	code := RunBuild([]string{dir})
	if code != 2 {
		t.Errorf("code = %d, want 2", code)
	}
}

func TestRunBuildRejectsNonexistentPath(t *testing.T) {
	silenceOutput(t)
	code := RunBuild([]string{"/definitely/not/a/real/path/xyz"})
	if code != 2 {
		t.Errorf("code = %d, want 2", code)
	}
}

func writeExpoPackageJSON(t *testing.T, dir string) {
	t.Helper()
	if err := os.WriteFile(filepath.Join(dir, "package.json"),
		[]byte(`{"name":"x","version":"1.0.0","dependencies":{"expo":"^52.0.0"}}`), 0o644); err != nil {
		t.Fatal(err)
	}
}

func TestStripAnsiEscapesRemovesCsiAndOscSequences(t *testing.T) {
	cases := map[string]string{
		"\x1b[31mred\x1b[0m":   "red",
		"plain text":           "plain text",
		"\x1b[1G\x1b[0K":       "",
		"a\x1b]0;title\x07b":   "ab",
		"tab\ttab":             "tab\ttab",
		"\x1b[1G\x1b[0K|hello": "|hello",
	}
	for in, want := range cases {
		if got := stripAnsiEscapes(in); got != want {
			t.Errorf("stripAnsiEscapes(%q) = %q, want %q", in, got, want)
		}
	}
}

func TestToHostArtifactPathTranslatesContainerPrefix(t *testing.T) {
	got := toHostArtifactPath("/home/dev/myapp", "/work/app/ebl_builds/v1-build1/app.apk")
	want := filepath.Join("/home/dev/myapp", "ebl_builds", "v1-build1", "app.apk")
	if got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestToHostArtifactPathPassesThroughUnknownPrefix(t *testing.T) {
	got := toHostArtifactPath("/home/dev/myapp", "/some/other/path.apk")
	if got != "/some/other/path.apk" {
		t.Errorf("got %q, want unchanged", got)
	}
}

func TestFormatBytesUsesKBBelowOneMB(t *testing.T) {
	if got := formatBytes(500 * 1024); got != "500 KB" {
		t.Errorf("got %q", got)
	}
}

func TestFormatBytesUsesMBAtOrAboveOneMB(t *testing.T) {
	if got := formatBytes(2 * 1024 * 1024); got != "2.0 MB" {
		t.Errorf("got %q", got)
	}
}

func TestFormatBuildDurationOmitsMinutesWhenZero(t *testing.T) {
	if got := formatBuildDuration(45); got != "45s" {
		t.Errorf("got %q", got)
	}
}

func TestFormatBuildDurationIncludesMinutes(t *testing.T) {
	if got := formatBuildDuration(125); got != "2m 5s" {
		t.Errorf("got %q", got)
	}
}

func TestProjectTokenFileRoundTrip(t *testing.T) {
	dir := t.TempDir()
	if err := saveProjectTokenFile(dir, "my-secret-token"); err != nil {
		t.Fatalf("saveProjectTokenFile: %v", err)
	}
	token, found := readProjectTokenFile(dir)
	if !found || token != "my-secret-token" {
		t.Errorf("token=%q found=%v", token, found)
	}

	gitignore, err := os.ReadFile(filepath.Join(dir, ".gitignore"))
	if err != nil {
		t.Fatalf("reading .gitignore: %v", err)
	}
	if !contains(string(gitignore), ".ebl-token") {
		t.Errorf(".gitignore = %q, want it to contain .ebl-token", gitignore)
	}
}

func TestProjectTokenFileMissingReturnsNotFound(t *testing.T) {
	dir := t.TempDir()
	_, found := readProjectTokenFile(dir)
	if found {
		t.Error("expected not found for a project with no .ebl-token file")
	}
}

func TestEnsureGitignoredDoesNotDuplicateAnExistingEntry(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, ".gitignore"), []byte("node_modules\n.ebl-token\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := ensureGitignored(dir, ".ebl-token"); err != nil {
		t.Fatalf("ensureGitignored: %v", err)
	}
	data, err := os.ReadFile(filepath.Join(dir, ".gitignore"))
	if err != nil {
		t.Fatal(err)
	}
	if got := string(data); contains(got, ".ebl-token\n.ebl-token") {
		t.Errorf(".gitignore has a duplicate entry: %q", got)
	}
}
