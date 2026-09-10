// Command readkeyonce is a test-only harness for tuiinput_pty_test.go: calls
// ReadKey() exactly once and exits - used to check Ctrl-C's real-SIGINT
// behavior without a second call racing the process teardown.
package main

import "github.com/41vi4p/expo-builder-local/cli/internal/tuiinput"

func main() { tuiinput.ReadKey() }
