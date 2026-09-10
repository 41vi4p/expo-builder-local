package pullprogress

import (
	"bytes"
	"strings"
	"testing"
)

func TestNonTTYPrintsPlainLinesForEveryEvent(t *testing.T) {
	var buf bytes.Buffer
	r := newRenderer(&buf, false)

	r.OnEvent("layer1", "Downloading", "[==>   ]  1MB/5MB")
	r.OnEvent("layer1", "Downloading", "[====> ]  3MB/5MB")
	r.OnEvent("", "Status: Downloaded newer image for x", "")

	out := buf.String()
	lines := strings.Split(strings.TrimRight(out, "\n"), "\n")
	if len(lines) != 3 {
		t.Fatalf("got %d lines, want 3: %q", len(lines), out)
	}
	if lines[0] != "layer1: Downloading [==>   ]  1MB/5MB" {
		t.Errorf("lines[0] = %q", lines[0])
	}
	if lines[1] != "layer1: Downloading [====> ]  3MB/5MB" {
		t.Errorf("lines[1] = %q", lines[1])
	}
	if lines[2] != "Status: Downloaded newer image for x" {
		t.Errorf("lines[2] = %q", lines[2])
	}
	// Non-TTY output must never contain ANSI redraw escapes.
	if strings.Contains(out, "\x1b[") {
		t.Errorf("non-TTY output must not contain ANSI escapes: %q", out)
	}
}

func TestTTYRedrawsInPlaceForARepeatedLayerID(t *testing.T) {
	var buf bytes.Buffer
	r := newRenderer(&buf, true)

	r.OnEvent("layer1", "Downloading", "[==>   ]  1MB/5MB")
	first := buf.String()
	if strings.Contains(first, "\x1b[") {
		t.Errorf("first occurrence of a new id must be a plain line, got %q", first)
	}

	buf.Reset()
	r.OnEvent("layer1", "Downloading", "[====> ]  3MB/5MB")
	second := buf.String()
	if !strings.Contains(second, "\x1b[1A") {
		t.Errorf("expected a cursor-up redraw escape, got %q", second)
	}
	if !strings.Contains(second, "layer1: Downloading [====> ]  3MB/5MB") {
		t.Errorf("expected the updated text in the redraw, got %q", second)
	}
}

func TestTTYPlainStatusLineIsNeverRedrawnInPlace(t *testing.T) {
	var buf bytes.Buffer
	r := newRenderer(&buf, true)

	r.OnEvent("", "Status: Downloaded newer image for x", "")
	r.OnEvent("", "Status: Downloaded newer image for x", "")

	out := buf.String()
	if strings.Contains(out, "\x1b[") {
		t.Errorf("empty-id status lines must never redraw in place, got %q", out)
	}
	if got := strings.Count(out, "Status: Downloaded newer image for x"); got != 2 {
		t.Errorf("expected 2 separate plain lines, got %d in %q", got, out)
	}
}

func TestTTYTracksMultipleLayersIndependently(t *testing.T) {
	var buf bytes.Buffer
	r := newRenderer(&buf, true)

	r.OnEvent("layer1", "Downloading", "")
	r.OnEvent("layer2", "Downloading", "")
	buf.Reset()

	// layer1 is now 2 lines up from the cursor (layer2's line was printed
	// after it).
	r.OnEvent("layer1", "Extracting", "")
	if got := buf.String(); !strings.Contains(got, "\x1b[2A") {
		t.Errorf("expected a 2-line-up redraw for layer1, got %q", got)
	}
}
