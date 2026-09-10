package winpath

import "testing"

// toDockerBindPathWindows is exercised directly (not through ToDockerBindPath,
// which is a runtime.GOOS no-op on this test's actual platform) - same
// reasoning as the C++ codebase testing platform-specific logic without
// needing the real OS.

func TestConvertsDriveLetterBackslashPath(t *testing.T) {
	got := toDockerBindPathWindows(`D:\Projects\App`)
	want := "//d/Projects/App"
	if got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestConvertsDriveLetterForwardSlashPath(t *testing.T) {
	got := toDockerBindPathWindows("C:/Users/dev/app")
	want := "//c/Users/dev/app"
	if got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestLowercasesTheDriveLetter(t *testing.T) {
	got := toDockerBindPathWindows(`D:\app`)
	if got[2] != 'd' {
		t.Errorf("got %q, want a lowercase drive letter", got)
	}
}

func TestPassesThroughAlreadyForwardSlashedDriveless(t *testing.T) {
	// Shouldn't normally happen (every caller derives from an absolute
	// Windows path), but harmless to leave alone.
	got := toDockerBindPathWindows("//already/converted")
	if got != "//already/converted" {
		t.Errorf("got %q, want unchanged", got)
	}
}

func TestPassesThroughTooShortToBeADrivePath(t *testing.T) {
	got := toDockerBindPathWindows("a")
	if got != "a" {
		t.Errorf("got %q, want unchanged", got)
	}
}
