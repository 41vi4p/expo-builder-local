package prompt

import (
	"bufio"
	"strings"
	"testing"
)

// withStdin temporarily swaps the package's shared stdinReader for one
// backed by input, restoring the original afterward.
func withStdin(t *testing.T, input string) {
	t.Helper()
	old := stdinReader
	stdinReader = bufio.NewReader(strings.NewReader(input))
	t.Cleanup(func() { stdinReader = old })
}

func TestStringReturnsTypedLine(t *testing.T) {
	withStdin(t, "hello\n")
	if got := String("Question", "default"); got != "hello" {
		t.Errorf("got %q, want %q", got, "hello")
	}
}

func TestStringReturnsDefaultOnEmptyLine(t *testing.T) {
	withStdin(t, "\n")
	if got := String("Question", "default"); got != "default" {
		t.Errorf("got %q, want %q", got, "default")
	}
}

func TestStringReturnsDefaultOnEOF(t *testing.T) {
	withStdin(t, "")
	if got := String("Question", "default"); got != "default" {
		t.Errorf("got %q, want %q", got, "default")
	}
}

func TestStringTrimsCarriageReturn(t *testing.T) {
	// A line arriving with a trailing \r (e.g. input piped from a
	// Windows-style CRLF source) shouldn't leak into the returned value.
	withStdin(t, "hello\r\n")
	if got := String("Question", "default"); got != "hello" {
		t.Errorf("got %q, want %q", got, "hello")
	}
}

func TestIntParsesAValidNumber(t *testing.T) {
	withStdin(t, "42\n")
	if got := Int("Question", 7); got != 42 {
		t.Errorf("got %d, want 42", got)
	}
}

func TestIntUsesDefaultOnEmptyLine(t *testing.T) {
	withStdin(t, "\n")
	if got := Int("Question", 7); got != 7 {
		t.Errorf("got %d, want 7", got)
	}
}

func TestIntReprompts(t *testing.T) {
	// "not-a-number" is rejected and re-prompted; "42" is accepted next.
	withStdin(t, "not-a-number\n42\n")
	if got := Int("Question", 7); got != 42 {
		t.Errorf("got %d, want 42", got)
	}
}

func TestYesNoAcceptsVariousAffirmativeSpellings(t *testing.T) {
	for _, in := range []string{"y\n", "Y\n", "yes\n", "Yes\n"} {
		withStdin(t, in)
		if !YesNo("Proceed?") {
			t.Errorf("YesNo(%q) = false, want true", in)
		}
	}
}

func TestYesNoRejectsAnythingElse(t *testing.T) {
	for _, in := range []string{"n\n", "no\n", "\n", "YES\n", "maybe\n"} {
		withStdin(t, in)
		if YesNo("Proceed?") {
			t.Errorf("YesNo(%q) = true, want false", in)
		}
	}
}

func TestHiddenFallsBackToPlainReadWhenStdinIsNotATerminal(t *testing.T) {
	// The test process's stdin is not a real terminal, so Hidden() always
	// takes its non-tty fallback path here - which is exactly the path this
	// test exercises via the swapped stdinReader.
	withStdin(t, "secret-token\n")
	if got := Hidden("Token"); got != "secret-token" {
		t.Errorf("got %q, want %q", got, "secret-token")
	}
}
