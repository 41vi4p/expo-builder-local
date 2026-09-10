// Package pullprogress renders `docker pull`-style progress: each layer id
// gets a single line that's updated in place (cursor-up + redraw) instead of
// a new line scrolling past for every progress tick. Falls back to plain
// line-by-line output when the output isn't a terminal (piped/redirected),
// same as the real `docker` CLI does. Direct port of cli/src/pull_progress.cpp.
package pullprogress

import (
	"fmt"
	"io"

	"golang.org/x/term"
)

// Renderer writes to Out (an io.Writer implementing a Fd() int method for the
// isTTY check, e.g. os.Stdout/os.Stderr) - pass os.Stderr when stdout needs
// to stay reserved for something else (e.g. `ebl build --json`'s final JSON
// result). The cursor-redraw-in-place behavior only activates when the
// chosen stream is itself a real terminal.
type Renderer struct {
	out     io.Writer
	isTTY   bool
	order   []string
	indexOf map[string]int
}

type fdWriter interface {
	io.Writer
	Fd() uintptr
}

// New returns a Renderer writing to out. If out doesn't expose a file
// descriptor (e.g. a plain bytes.Buffer in a test), it's treated as
// non-terminal - the same "not a tty" fallback the real CLI uses for any
// redirected stream.
func New(out fdWriter) *Renderer {
	return newRenderer(out, term.IsTerminal(int(out.Fd())))
}

func newRenderer(out io.Writer, isTTY bool) *Renderer {
	return &Renderer{out: out, isTTY: isTTY, indexOf: map[string]int{}}
}

// OnEvent handles one pull-progress event. id is the layer's short hash, or
// empty for a plain status line (e.g. "Status: Downloaded newer image for
// ..."), which is always printed as its own line and never redrawn in place.
// progress is a pre-rendered bar string (e.g. "[==>       ]  1.2MB/5MB") -
// may be empty.
func (r *Renderer) OnEvent(id, status, progress string) {
	text := status
	if id != "" {
		text = id + ": " + status
	}
	if progress != "" {
		text += " " + progress
	}

	if !r.isTTY || id == "" {
		fmt.Fprintln(r.out, text)
		return
	}

	idx, ok := r.indexOf[id]
	if !ok {
		r.indexOf[id] = len(r.order)
		r.order = append(r.order, text)
		fmt.Fprintln(r.out, text)
		return
	}

	r.order[idx] = text
	linesUp := len(r.order) - idx
	// Move up to the tracked line, clear it, redraw, then move back down to
	// the blank line below the last tracked one - the same spot every redraw
	// starts from.
	fmt.Fprintf(r.out, "\x1b[%dA\r\x1b[2K%s\x1b[%dB\r", linesUp, text, linesUp)
}
