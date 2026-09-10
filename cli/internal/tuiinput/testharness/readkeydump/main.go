// Command readkeydump is a test-only harness for tuiinput_pty_test.go: calls
// ReadKey() 4 times and prints each result. Lives inside the module tree
// (not a dynamically-written /tmp source file) specifically so it can import
// the internal/tuiinput package at all - Go's internal-package visibility
// rule requires that.
package main

import (
	"fmt"

	"github.com/41vi4p/expo-builder-local/cli/internal/tuiinput"
)

func main() {
	names := map[tuiinput.Key]string{
		tuiinput.Up: "Up", tuiinput.Down: "Down", tuiinput.Enter: "Enter",
		tuiinput.Escape: "Escape", tuiinput.Other: "Other",
	}
	for i := 0; i < 4; i++ {
		fmt.Printf("KEY:%s\n", names[tuiinput.ReadKey()])
	}
}
