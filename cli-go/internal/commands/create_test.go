package commands

import (
	"os"
	"path/filepath"
	"testing"
)

// silenceOutput redirects os.Stdout/os.Stderr to /dev/null for the duration
// of the test, restoring them afterward - these command functions print
// directly, and the tests below only care about exit codes and filesystem
// side effects, not the printed text.
func silenceOutput(t *testing.T) {
	t.Helper()
	devNull, err := os.OpenFile(os.DevNull, os.O_WRONLY, 0)
	if err != nil {
		t.Fatal(err)
	}
	oldOut, oldErr := os.Stdout, os.Stderr
	os.Stdout, os.Stderr = devNull, devNull
	t.Cleanup(func() {
		os.Stdout, os.Stderr = oldOut, oldErr
		devNull.Close()
	})
}

func TestCreateHelpReturnsZero(t *testing.T) {
	silenceOutput(t)
	if code := RunCreate([]string{"--help"}); code != 0 {
		t.Errorf("code = %d, want 0", code)
	}
}

func TestCreateMissingArgumentsReturnsUsageError(t *testing.T) {
	silenceOutput(t)
	if code := RunCreate([]string{}); code != 2 {
		t.Errorf("code = %d, want 2", code)
	}
	if code := RunCreate([]string{"."}); code != 2 {
		t.Errorf("code = %d, want 2 (name missing)", code)
	}
}

func TestCreateUnknownOptionReturnsError(t *testing.T) {
	silenceOutput(t)
	if code := RunCreate([]string{"--bogus", ".", "app"}); code != 2 {
		t.Errorf("code = %d, want 2", code)
	}
}

func TestCreateUnexpectedExtraArgumentReturnsError(t *testing.T) {
	silenceOutput(t)
	if code := RunCreate([]string{".", "app", "extra"}); code != 2 {
		t.Errorf("code = %d, want 2", code)
	}
}

func TestCreateTemplateMissingValueReturnsError(t *testing.T) {
	silenceOutput(t)
	if code := RunCreate([]string{".", "app", "--template"}); code != 2 {
		t.Errorf("code = %d, want 2", code)
	}
}

func TestCreateNonexistentPathReturnsError(t *testing.T) {
	silenceOutput(t)
	dir := t.TempDir()
	if code := RunCreate([]string{filepath.Join(dir, "does-not-exist"), "app"}); code != 2 {
		t.Errorf("code = %d, want 2", code)
	}
}

func TestCreatePathThatIsAFileReturnsError(t *testing.T) {
	silenceOutput(t)
	dir := t.TempDir()
	file := filepath.Join(dir, "notadir")
	if err := os.WriteFile(file, []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}
	if code := RunCreate([]string{file, "app"}); code != 2 {
		t.Errorf("code = %d, want 2", code)
	}
}

func TestCreateAlreadyExistingAppDirReturnsError(t *testing.T) {
	silenceOutput(t)
	dir := t.TempDir()
	if err := os.MkdirAll(filepath.Join(dir, "myapp"), 0o755); err != nil {
		t.Fatal(err)
	}
	if code := RunCreate([]string{dir, "myapp"}); code != 2 {
		t.Errorf("code = %d, want 2", code)
	}
}

// TestCreateNpxNotFoundReturnsClearError exercises the real hostprocess path
// (unlike the validation-only tests above) by making `npx` unresolvable via
// PATH, confirming the exit-127 "install Node.js" message path returns exit
// code 1 rather than a raw/confusing code.
func TestCreateNpxNotFoundReturnsClearError(t *testing.T) {
	silenceOutput(t)
	oldPath := os.Getenv("PATH")
	os.Setenv("PATH", "/nonexistent-path-for-test")
	t.Cleanup(func() { os.Setenv("PATH", oldPath) })

	dir := t.TempDir()
	code := RunCreate([]string{dir, "myapp"})
	if code != 1 {
		t.Errorf("code = %d, want 1", code)
	}
	// Must not have created the target directory on this failure path.
	if _, err := os.Stat(filepath.Join(dir, "myapp")); !os.IsNotExist(err) {
		t.Error("expected myapp to not have been created")
	}
}
