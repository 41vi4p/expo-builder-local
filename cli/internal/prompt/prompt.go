// Package prompt provides small interactive-prompt helpers shared by any
// command that needs to ask the user something at a terminal (currently
// `ebl config`'s wizard and `ebl build`'s missing-token prompt). Direct port
// of cli/src/prompt.cpp.
package prompt

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"strconv"
	"strings"

	"golang.org/x/term"

	"github.com/41vi4p/expo-builder-local/cli/internal/color"
)

// stdinReader is the single buffered reader every prompt in this package
// reads through - including the hidden-input path, which only ever toggles
// terminal echo off/on around a read from this same reader rather than
// dropping to a separate raw fd read. Mixing a buffered reader with a raw,
// bypass-the-buffer read on the same fd is a known-unsafe pattern in this
// codebase (see cli/CLAUDE.md's tui_input.cpp history: a raw select() call
// missed bytes bufio-equivalent stdio buffering had already consumed) - this
// package sidesteps that class of bug entirely by never using a second,
// independent reader against stdin.
var stdinReader = bufio.NewReader(os.Stdin)

// SetInputForTesting swaps the shared stdin reader for r, returning a
// restore function. For use by other packages' tests that exercise a full
// interactive command (e.g. `ebl config`'s wizard) end to end - never call
// this from production code.
func SetInputForTesting(r io.Reader) (restore func()) {
	old := stdinReader
	stdinReader = bufio.NewReader(r)
	return func() { stdinReader = old }
}

func readLine() (string, error) {
	line, err := stdinReader.ReadString('\n')
	if err != nil && err != io.EOF {
		return "", err
	}
	return strings.TrimRight(line, "\r\n"), nil
}

// String prints "question [defaultValue]: " and returns the typed line, or
// defaultValue if the user just pressed Enter.
func String(question, defaultValue string) string {
	fmt.Print(question)
	if defaultValue != "" {
		fmt.Printf(" [%s]", defaultValue)
	}
	fmt.Print(": ")

	line, err := readLine()
	if err != nil || line == "" {
		return defaultValue
	}
	return line
}

// Int repeatedly prompts until a whole number is entered.
func Int(question string, defaultValue int) int {
	for {
		raw := String(question, strconv.Itoa(defaultValue))
		if n, err := strconv.Atoi(raw); err == nil {
			return n
		}
		fmt.Println(color.Red("Enter a whole number."))
	}
}

// YesNo prints "question [y/N] " and reports whether the user answered
// affirmatively (y/Y/yes/Yes) - anything else, including a read error/EOF,
// is treated as "no". Matches cli/src/commands/setup.cpp's promptYesNo.
func YesNo(question string) bool {
	fmt.Print(question, " [y/N] ")
	line, err := readLine()
	if err != nil {
		return false
	}
	switch line {
	case "y", "Y", "yes", "Yes":
		return true
	default:
		return false
	}
}

// Hidden is like String but with terminal echo disabled - used for anything
// token/password-shaped. Falls back to a normal (echoed) read if stdin isn't
// actually a terminal (e.g. piped input in a script).
func Hidden(question string) string {
	fmt.Print(question, ": ")

	fd := int(os.Stdin.Fd())
	if !term.IsTerminal(fd) {
		line, _ := readLine()
		return line
	}

	restore, err := disableEcho(fd)
	if err != nil {
		// Best-effort: if toggling echo fails for some reason, still read the
		// line (echoed) rather than hanging or erroring outright.
		line, _ := readLine()
		fmt.Println()
		return line
	}
	line, _ := readLine()
	restore()
	fmt.Println()
	return line
}
