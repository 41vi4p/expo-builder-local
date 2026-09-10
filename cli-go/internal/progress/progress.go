// Package progress turns the runner's raw output lines into a single,
// monotonically-increasing 0-100 build percentage - a direct Go port of
// cli/src/build_progress_tracker.cpp (itself a port of
// orchestrator/src/build/progress.ts's `ProgressTracker`).
//
// Why this exists instead of just using build-entrypoint.sh's own
// `@@PROGRESS:` marker directly: that marker is only ever emitted once, right
// as a phase finishes, for most phases - and never during `gradle`/`eas`, the
// two phases that actually take minutes. A build using only the raw marker
// value would show the progress bar stuck at 0% for the entire gradle/eas
// phase.
//
// This blends three signals instead: (1) `@@PHASE:`/`@@PROGRESS:` markers
// against fixed per-phase weights, so the overall percent is monotonic across
// phase boundaries; (2) Gradle's own live `NN% EXECUTING/CONFIGURING/
// INITIALIZING` console line (available because the build container gets a
// real TTY and Gradle runs with `--console=rich`); (3) a short list of
// recognizable milestone strings in `eas build --local`'s own output, since
// eas-cli never prints a live percentage the way Gradle does. Pure logic, no
// I/O.
package progress

import (
	"regexp"
	"strconv"
	"strings"
)

var phaseOrder = []string{"setup", "install", "prebuild", "signing", "gradle", "eas", "collect", "done"}

// Same weights/order as orchestrator/src/build/progress.ts's GRADLE_WEIGHTS/
// EAS_WEIGHTS - keep both in sync by hand if either changes.
var gradleWeights = []int{2, 15, 10, 3, 65, 0, 5, 0}
var easWeights = []int{2, 15, 0, 3, 0, 75, 5, 0}

func phaseIndexOf(id string) int {
	for i, p := range phaseOrder {
		if p == id {
			return i
		}
	}
	return -1
}

type milestone struct {
	pattern  *regexp.Regexp
	fraction float64
}

// Coarse, best-effort milestones within `eas build --local` output - eas-cli
// never prints a live percentage the way Gradle does, so this steps through
// recognizable phase-transition lines instead. Same list/order/fractions as
// progress.ts's EAS_MILESTONES.
var easMilestones = []milestone{
	{regexp.MustCompile(`(?i)Resolving credentials`), 0.1},
	{regexp.MustCompile(`(?i)Compressing project`), 0.2},
	{regexp.MustCompile(`(?i)Prebuild|Running.*prebuild`), 0.35},
	{regexp.MustCompile(`(?i)Installing`), 0.45},
	{regexp.MustCompile(`(?i)Running gradle|Gradle build`), 0.55},
	{regexp.MustCompile(`(?i)BUILD SUCCESSFUL`), 0.95},
}

// Same pattern as progress.ts's GRADLE_PROGRESS_RE - matches Gradle's own
// `--console=rich` live line, e.g. "<====-----> 63% EXECUTING".
var gradleProgressRE = regexp.MustCompile(`(\d{1,3})%\s+(EXECUTING|CONFIGURING|INITIALIZING)`)

// Tracker blends marker/weight/regex/milestone signals into one monotonic
// build percentage. Zero value is ready to use (engine defaults to "gradle",
// matching build-entrypoint.sh's own two-engine model).
type Tracker struct {
	engine          string
	phaseIndex      int
	percent         int
	easMilestoneIdx int
}

// SetEngine selects which per-phase weight table applies and whether
// Gradle-console or eas-milestone parsing is active during the corresponding
// phase. Anything other than "eas" is treated as "gradle".
func (t *Tracker) SetEngine(engine string) {
	if engine == "eas" {
		t.engine = "eas"
	} else {
		t.engine = "gradle"
	}
}

func (t *Tracker) weights() []int {
	if t.engine == "eas" {
		return easWeights
	}
	return gradleWeights
}

func (t *Tracker) baseForCurrentPhase() int {
	weights := t.weights()
	base := 0
	for i := 0; i < t.phaseIndex && i < len(phaseOrder); i++ {
		base += weights[i]
	}
	return base
}

func (t *Tracker) weightForCurrentPhase() int {
	if t.phaseIndex < 0 || t.phaseIndex >= len(phaseOrder) {
		return 0
	}
	return t.weights()[t.phaseIndex]
}

func (t *Tracker) clampAdvance(pct float64) int {
	rounded := int(pct + 0.5)
	if rounded > t.percent {
		t.percent = rounded
	}
	if t.percent > 100 {
		t.percent = 100
	}
	return t.percent
}

// HandleLine feeds one line of runner output through the tracker - marker or
// not - and returns the new overall percent (always >= whatever was returned
// before) if this line changed it, or ok=false if the line didn't affect
// progress at all.
func (t *Tracker) HandleLine(line string) (percent int, ok bool) {
	if rest, found := strings.CutPrefix(line, "@@PHASE:"); found {
		id := rest
		if sep := strings.IndexByte(rest, ':'); sep != -1 {
			id = rest[:sep]
		}
		idx := phaseIndexOf(id)
		if idx < 0 {
			return 0, false // unrecognized phase id - ignore rather than guess
		}
		t.phaseIndex = idx
		t.easMilestoneIdx = 0
		return t.clampAdvance(float64(t.baseForCurrentPhase())), true
	}

	if rest, found := strings.CutPrefix(line, "@@PROGRESS:"); found {
		within, err := strconv.Atoi(rest)
		if err != nil {
			return 0, false // malformed - keep whatever was there
		}
		if within < 0 {
			within = 0
		} else if within > 100 {
			within = 100
		}
		fraction := float64(within) / 100.0
		return t.clampAdvance(float64(t.baseForCurrentPhase()) + float64(t.weightForCurrentPhase())*fraction), true
	}

	inGradlePhase := t.engine != "eas" && t.phaseIndex >= 0 && t.phaseIndex < len(phaseOrder) &&
		phaseOrder[t.phaseIndex] == "gradle"
	if inGradlePhase {
		m := gradleProgressRE.FindStringSubmatch(line)
		if m == nil {
			return 0, false
		}
		n, _ := strconv.Atoi(m[1])
		fraction := float64(n) / 100.0
		return t.clampAdvance(float64(t.baseForCurrentPhase()) + float64(t.weightForCurrentPhase())*fraction), true
	}

	inEasPhase := t.engine == "eas" && t.phaseIndex >= 0 && t.phaseIndex < len(phaseOrder) &&
		phaseOrder[t.phaseIndex] == "eas"
	if inEasPhase {
		for i := t.easMilestoneIdx; i < len(easMilestones); i++ {
			if easMilestones[i].pattern.MatchString(line) {
				t.easMilestoneIdx = i + 1
				return t.clampAdvance(float64(t.baseForCurrentPhase()) + float64(t.weightForCurrentPhase())*easMilestones[i].fraction), true
			}
		}
		return 0, false
	}

	return 0, false
}
