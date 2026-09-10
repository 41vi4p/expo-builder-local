package color

import "testing"

func TestWrapWithEnabledFalseReturnsPlainText(t *testing.T) {
	if got := wrapWithEnabled(false, "31", "hello"); got != "hello" {
		t.Errorf("got %q, want %q", got, "hello")
	}
}

func TestWrapWithEnabledTrueAddsAnsiCodes(t *testing.T) {
	got := wrapWithEnabled(true, "31", "hello")
	want := "\x1b[31mhello\x1b[0m"
	if got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestHelpersUseDistinctCodes(t *testing.T) {
	cases := []struct {
		name string
		fn   func(string) string
		code string
	}{
		{"Bold", Bold, "1"},
		{"Dim", Dim, "2"},
		{"Red", Red, "31"},
		{"Green", Green, "32"},
		{"Yellow", Yellow, "33"},
		{"Cyan", Cyan, "36"},
	}
	for _, tc := range cases {
		want := wrapWithEnabled(Enabled(), tc.code, "x")
		if got := tc.fn("x"); got != want {
			t.Errorf("%s(%q) = %q, want %q", tc.name, "x", got, want)
		}
	}
}

// Enabled()/StdinIsTTY() are computed once from real fds and can't be forced
// either way in a unit test (this test process's stdout/stdin are whatever
// the test runner gave it, typically not a real tty) - TestHelpersUseDistinctCodes
// above verifies the wiring is correct regardless of which state that
// happens to be, rather than asserting a specific value for Enabled() itself.
func TestEnabledIsStableAcrossCalls(t *testing.T) {
	if Enabled() != Enabled() {
		t.Error("Enabled() must be stable across calls within one process")
	}
}
