// Package buildstatusview renders a live, redraw-in-place build-status
// dashboard to stdout: current phase, a progress bar, elapsed time,
// CPU/memory current values, and a short tail of recent build-tool log
// lines. Plain ASCII only (no Unicode block characters) - deliberately, so
// this renders correctly on a legacy Windows console codepage without
// needing a global UTF-8 console-output change. Direct port of
// cli/src/build_status_view.hpp/.cpp.
package buildstatusview

import (
	"fmt"
	"strings"

	"github.com/41vi4p/expo-builder-local/cli/internal/color"
)

const maxLogLineLen = 100

// State is everything View needs for one frame - the caller (the build
// command) owns this, updates it as markers/stats/log lines arrive, and
// passes it to Render() on each tick. RecentLogLines is oldest-first; the
// caller caps its length itself, to keep a long build's memory footprint
// bounded.
type State struct {
	PhaseID         string
	PhaseLabel      string
	ProgressPercent int
	ElapsedSeconds  int64
	CPUPercent      float64
	MemUsedMB       float64
	MemLimitMB      float64
	RecentLogLines  []string
}

// NewState returns a State with the documented defaults, matching the C++
// struct's default member initializers.
func NewState() State {
	return State{PhaseID: "setup", PhaseLabel: "Starting..."}
}

func formatElapsed(seconds int64) string {
	m := seconds / 60
	s := seconds % 60
	return fmt.Sprintf("%02d:%02d", m, s)
}

func renderProgressBar(percent, width int) string {
	if percent < 0 {
		percent = 0
	} else if percent > 100 {
		percent = 100
	}
	filled := (percent * width) / 100
	var bar strings.Builder
	bar.WriteByte('[')
	bar.WriteString(strings.Repeat("=", filled))
	bar.WriteString(strings.Repeat("-", width-filled))
	bar.WriteString(fmt.Sprintf("] %d%%", percent))
	return bar.String()
}

func truncate(s string, maxLen int) string {
	if len(s) > maxLen {
		return s[:maxLen] + "..."
	}
	return s
}

// formatMemory auto-scales to GB above 1024MB (a build container's memory
// limit is routinely several GB, and "4096MB" reads worse than "4.00GB") -
// same MB/GB-style threshold the build command's own formatBytes uses for
// KB vs. MB.
func formatMemory(mb float64) string {
	if mb >= 1024.0 {
		return fmt.Sprintf("%.2fGB", mb/1024.0)
	}
	return fmt.Sprintf("%.0fMB", mb)
}

// View renders repeated frames of the dashboard, redrawing in place. Meant
// to be constructed once per build and have Render() called repeatedly
// (e.g. once a second) - it tracks how many lines the previous frame used
// so it can move the cursor back up and clear before repainting, the same
// technique pullprogress uses for a single line, extended to a whole block.
type View struct {
	appLabel      string
	linesRendered int
}

// New returns a View. appLabel is shown once in the header (e.g. the
// project path being built).
func New(appLabel string) *View { return &View{appLabel: appLabel} }

// Render paints one frame of the dashboard for state.
func (v *View) Render(state State) {
	var out strings.Builder
	out.WriteString("\n" + color.Bold("Building "+color.Cyan(v.appLabel)) + "\n\n")
	out.WriteString("  " + color.Dim("Phase   ") + "  " + state.PhaseID + " - " + state.PhaseLabel + "\n")
	out.WriteString("  " + color.Dim("Progress") + "  " + renderProgressBar(state.ProgressPercent, 30) + "\n")
	out.WriteString("  " + color.Dim("Elapsed ") + "  " + formatElapsed(state.ElapsedSeconds) + "\n\n")

	out.WriteString("  " + color.Dim("CPU     ") + "  " + fmt.Sprintf("%5.1f%%", state.CPUPercent) + "\n")

	memPercent := 0.0
	if state.MemLimitMB > 0 {
		memPercent = (state.MemUsedMB / state.MemLimitMB) * 100.0
	}
	out.WriteString("  " + color.Dim("Memory  ") + "  " + formatMemory(state.MemUsedMB) + " / " +
		formatMemory(state.MemLimitMB) + fmt.Sprintf(" (%.1f%%)\n", memPercent))

	if len(state.RecentLogLines) > 0 {
		out.WriteString("\n")
		for _, line := range state.RecentLogLines {
			out.WriteString("  " + color.Dim(truncate(line, maxLogLineLen)) + "\n")
		}
	}

	frame := out.String()
	newLineCount := strings.Count(frame, "\n")

	if v.linesRendered > 0 {
		// Move up to the start of the previous frame, then clear everything
		// from there to the end of the screen before repainting - a plain
		// per-line clear wouldn't erase a line that existed last frame (e.g.
		// a log line) but isn't part of this one.
		fmt.Printf("\x1b[%dA\x1b[0J", v.linesRendered)
	}
	fmt.Print(frame)
	v.linesRendered = newLineCount
}
