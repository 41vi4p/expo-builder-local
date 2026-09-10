package commands

import (
	"os"
	"testing"
)

func TestCompletionMissingShellNameReturnsError(t *testing.T) {
	silenceOutput(t)
	if code := RunCompletion(nil); code != 2 {
		t.Errorf("code = %d, want 2", code)
	}
}

func TestCompletionUnknownShellReturnsError(t *testing.T) {
	silenceOutput(t)
	if code := RunCompletion([]string{"tcsh"}); code != 2 {
		t.Errorf("code = %d, want 2", code)
	}
}

func TestCompletionHelpReturnsZero(t *testing.T) {
	silenceOutput(t)
	if code := RunCompletion([]string{"--help"}); code != 0 {
		t.Errorf("code = %d, want 0", code)
	}
}

func TestCompletionPwshAliasWorks(t *testing.T) {
	silenceOutput(t)
	if code := RunCompletion([]string{"pwsh"}); code != 0 {
		t.Errorf("code = %d, want 0", code)
	}
}

// TestCompletionScriptsMatchCppByteForByte cross-checks each script constant
// against the real C++ CLI's own output for the same shell, captured to
// cli-go/internal/commands/testdata/completion_cpp_<shell>.txt from a real
// build (see the session history in docs/CHANGELOG.md around the Go
// migration for how these fixtures were produced) - this is ~250 lines of
// hand-transcribed shell script, so a byte-for-byte diff against the
// original is the only reliable way to catch a transcription slip.
func TestCompletionScriptsMatchCppByteForByte(t *testing.T) {
	cases := map[string]string{
		"bash":       bashCompletionScript,
		"zsh":        zshCompletionScript,
		"fish":       fishCompletionScript,
		"powershell": powershellCompletionScript,
	}
	for shell, script := range cases {
		want, err := os.ReadFile("testdata/completion_cpp_" + shell + ".txt")
		if err != nil {
			t.Fatalf("%s: reading fixture: %v", shell, err)
		}
		if script != string(want) {
			t.Errorf("%s completion script does not match the C++ CLI's output byte-for-byte", shell)
		}
	}
}
