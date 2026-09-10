// Command selectmenu is a test-only harness for tuimenu_pty_test.go: renders
// a 3-option menu with a caller-supplied default index and prints the final
// chosen index once the user confirms or cancels. Lives inside the module
// tree specifically so it can import internal/tuimenu at all.
package main

import (
	"fmt"
	"os"
	"strconv"

	"github.com/41vi4p/expo-builder-local/cli/internal/tuimenu"
)

func main() {
	defaultIndex := 0
	if len(os.Args) > 1 {
		defaultIndex, _ = strconv.Atoi(os.Args[1])
	}
	result := tuimenu.Select("Pick one", []string{"alpha", "beta", "gamma"}, defaultIndex)
	fmt.Printf("RESULT:%d\n", result)
}
