package buildstatusview

import (
	"io"
	"os"
	"strings"
	"testing"
)

// captureStdout redirects os.Stdout for the duration of fn, returning
// everything written to it.
func captureStdout(t *testing.T, fn func()) string {
	t.Helper()
	r, w, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	old := os.Stdout
	os.Stdout = w
	fn()
	w.Close()
	os.Stdout = old

	data, err := io.ReadAll(r)
	if err != nil {
		t.Fatal(err)
	}
	return string(data)
}

func TestNewStateHasDocumentedDefaults(t *testing.T) {
	s := NewState()
	if s.PhaseID != "setup" || s.PhaseLabel != "Starting..." {
		t.Errorf("NewState() = %+v", s)
	}
}

func TestRenderIncludesEveryFieldOnFirstFrame(t *testing.T) {
	view := New("my-app")
	s := NewState()
	s.PhaseID = "install"
	s.PhaseLabel = "Installing dependencies"
	s.ProgressPercent = 42
	s.ElapsedSeconds = 75
	s.CPUPercent = 12.5
	s.MemUsedMB = 512
	s.MemLimitMB = 2048
	s.RecentLogLines = []string{"npm install running"}

	out := captureStdout(t, func() { view.Render(s) })

	for _, want := range []string{
		"my-app", "install", "Installing dependencies",
		"42%", "01:15", "12.5%", "512MB", "2.00GB", "25.0%",
		"npm install running",
	} {
		if !strings.Contains(out, want) {
			t.Errorf("output missing %q\nfull output: %q", want, out)
		}
	}
}

func TestRenderRedrawsInPlaceOnSubsequentFrames(t *testing.T) {
	view := New("my-app")
	s := NewState()

	first := captureStdout(t, func() { view.Render(s) })
	if strings.Contains(first, "\x1b[") {
		t.Errorf("first frame must not contain a redraw escape (nothing to move up over yet), got %q", first)
	}

	s.ProgressPercent = 10
	second := captureStdout(t, func() { view.Render(s) })
	if !strings.Contains(second, "\x1b[") {
		t.Errorf("subsequent frame must redraw in place, got %q", second)
	}
}

func TestRenderTruncatesLongLogLines(t *testing.T) {
	view := New("my-app")
	s := NewState()
	longLine := strings.Repeat("x", 150)
	s.RecentLogLines = []string{longLine}

	out := captureStdout(t, func() { view.Render(s) })
	if strings.Contains(out, longLine) {
		t.Error("expected the 150-char line to be truncated, found it unmodified in the output")
	}
	if !strings.Contains(out, strings.Repeat("x", 100)+"...") {
		t.Error("expected a 100-char prefix followed by '...'")
	}
}

func TestRenderOmitsLogSectionWhenEmpty(t *testing.T) {
	view := New("my-app")
	out := captureStdout(t, func() { view.Render(NewState()) })
	// No trailing blank-line-then-content block for logs - just confirm no
	// stray leftover content appears where log lines would go.
	if strings.Count(out, "\n\n") > 2 {
		t.Errorf("expected no extra blank sections when RecentLogLines is empty, got %q", out)
	}
}

// TestMatchesCppOutputByteForByte cross-checks Render's output against the
// real C++ CLI's own build_status_view.cpp for the identical fabricated
// state, captured to testdata/cpp_render_output.txt from a real build (see
// docs/CHANGELOG.md for how this fixture was produced during the Go
// migration) - locks in exact visual parity with the shipped C++ dashboard.
func TestMatchesCppOutputByteForByte(t *testing.T) {
	want, err := os.ReadFile("testdata/cpp_render_output.txt")
	if err != nil {
		t.Fatalf("reading fixture: %v", err)
	}

	view := New("smoke-test-app")
	s := NewState()
	s.PhaseID = "gradle"
	s.PhaseLabel = "Running Gradle build"
	s.ProgressPercent = 63
	s.ElapsedSeconds = 125
	s.CPUPercent = 233.4
	s.MemUsedMB = 2867.0
	s.MemLimitMB = 4096.0
	s.RecentLogLines = []string{"> Task :app:compileDebugKotlin", "> Task :app:mergeDebugResources"}

	got := captureStdout(t, func() { view.Render(s) })
	if got != string(want) {
		t.Errorf("output does not match the C++ CLI's output byte-for-byte\ngot:\n%q\nwant:\n%q", got, want)
	}
}
