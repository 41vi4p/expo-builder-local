// Package color provides minimal ANSI color helpers - no-op (plain text) when
// stdout isn't a terminal, e.g. when output is piped or redirected to a file.
// Direct port of cli/src/color.hpp.
package color

import (
	"os"
	"sync"

	"golang.org/x/term"
)

var enabledOnce = sync.OnceValue(func() bool {
	return term.IsTerminal(int(os.Stdout.Fd()))
})

// Enabled reports whether stdout is a real terminal - computed once and
// cached, same as the C++ version's function-local static bool (a terminal's
// TTY-ness never changes during a process's lifetime).
func Enabled() bool { return enabledOnce() }

var stdinIsTTYOnce = sync.OnceValue(func() bool {
	return term.IsTerminal(int(os.Stdin.Fd()))
})

// StdinIsTTY is the same idea as Enabled, but checks stdin - needed by
// anything that reads interactive input (e.g. the `--tui` menu, once
// ported), since that can't work at all if stdin is piped/redirected (no
// arrow-key presses will ever arrive), separately from whether stdout
// happens to be a real terminal.
func StdinIsTTY() bool { return stdinIsTTYOnce() }

func wrapWithEnabled(enabled bool, code, text string) string {
	if !enabled {
		return text
	}
	return "\x1b[" + code + "m" + text + "\x1b[0m"
}

func wrap(code, text string) string { return wrapWithEnabled(Enabled(), code, text) }

func Bold(s string) string   { return wrap("1", s) }
func Dim(s string) string    { return wrap("2", s) }
func Red(s string) string    { return wrap("31", s) }
func Green(s string) string  { return wrap("32", s) }
func Yellow(s string) string { return wrap("33", s) }
func Cyan(s string) string   { return wrap("36", s) }
