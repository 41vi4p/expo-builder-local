// Command render is a test-only fixture generator: renders one fabricated
// frame matching the exact scenario cli/src/build_status_view.cpp's own
// smoke test used, so the two outputs can be diffed byte-for-byte.
package main

import "github.com/41vi4p/expo-builder-local/cli/internal/buildstatusview"

func main() {
	view := buildstatusview.New("smoke-test-app")
	s := buildstatusview.NewState()
	s.PhaseID = "gradle"
	s.PhaseLabel = "Running Gradle build"
	s.ProgressPercent = 63
	s.ElapsedSeconds = 125
	s.CPUPercent = 233.4
	s.MemUsedMB = 2867.0
	s.MemLimitMB = 4096.0
	s.RecentLogLines = []string{"> Task :app:compileDebugKotlin", "> Task :app:mergeDebugResources"}
	view.Render(s)
}
