// Package tuiinput reads a single "navigation" keypress from the terminal in
// raw mode (no echo, no line buffering) - blocks until one arrives. Arrow
// keys are recognized via their platform-specific encoding and normalized to
// Up/Down; Enter/Return and Escape (or Ctrl-C, which raw mode stops the
// terminal from turning into a real SIGINT) are recognized directly;
// anything else comes back as Other for the caller to ignore. Direct port of
// cli/src/tui_input.hpp/.cpp.
package tuiinput

// Key is a normalized navigation keypress.
type Key int

const (
	Other Key = iota
	Up
	Down
	Enter
	Escape
)
