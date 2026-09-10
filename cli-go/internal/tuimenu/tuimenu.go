// Package tuimenu renders a navigable, single-select arrow-key menu (used by
// `ebl build --tui`), redrawing in place after every keypress. Direct port
// of cli/src/tui_menu.cpp.
package tuimenu

import (
	"fmt"
	"strings"

	"github.com/41vi4p/expo-builder-local/cli/internal/color"
	"github.com/41vi4p/expo-builder-local/cli/internal/tuiinput"
)

// Select renders options as a navigable, single-select menu under title
// (Up/Down to move, Enter to confirm, Escape/Ctrl-C to cancel), redrawing in
// place after every keypress - same ANSI cursor-movement technique
// pullprogress/build_status_view already use. Blocks until the user
// decides. Returns the chosen index, or -1 on cancellation. Needs a real
// terminal on both stdout and stdin - callers must check that themselves
// first (e.g. color.Enabled() + color.StdinIsTTY()), this doesn't re-check.
func Select(title string, options []string, defaultIndex int) int {
	if len(options) == 0 {
		return -1
	}
	selected := defaultIndex
	if selected < 0 {
		selected = 0
	}
	if selected > len(options)-1 {
		selected = len(options) - 1
	}
	linesRendered := 0

	render := func() {
		var frame strings.Builder
		frame.WriteString("\n" + color.Bold(title) + "\n")
		for i, opt := range options {
			isSelected := i == selected
			prefix := "  "
			if isSelected {
				prefix = "> "
			}
			line := prefix + opt
			if isSelected {
				line = color.Cyan(color.Bold(line))
			}
			frame.WriteString(line + "\n")
		}
		text := frame.String()
		newLineCount := strings.Count(text, "\n")
		if linesRendered > 0 {
			fmt.Printf("\x1b[%dA\x1b[0J", linesRendered)
		}
		fmt.Print(text)
		linesRendered = newLineCount
	}

	render()
	for {
		switch tuiinput.ReadKey() {
		case tuiinput.Up:
			selected = (selected - 1 + len(options)) % len(options)
			render()
		case tuiinput.Down:
			selected = (selected + 1) % len(options)
			render()
		case tuiinput.Enter:
			return selected
		case tuiinput.Escape:
			return -1
		case tuiinput.Other:
			// ignored - wait for the next key
		}
	}
}
