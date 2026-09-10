package progress

import "testing"

// Ported 1:1 from cli/tests/test_build_progress_tracker.cpp - same expected
// values, already numerically verified against the C++ implementation.

func mustAdvance(t *testing.T, got int, ok bool, want int) {
	t.Helper()
	if !ok {
		t.Fatalf("expected a value, got none")
	}
	if got != want {
		t.Errorf("got %d, want %d", got, want)
	}
}

func TestPhaseMarkerJumpsToThatPhaseBaseWeight(t *testing.T) {
	var tr Tracker
	tr.SetEngine("gradle")
	// Gradle weights: setup=2, install=15, prebuild=10, signing=3, gradle=65, ...
	p1, ok := tr.HandleLine("@@PHASE:setup:Starting")
	mustAdvance(t, p1, ok, 0) // base for phase index 0 is 0

	p2, ok := tr.HandleLine("@@PHASE:install:Installing deps")
	mustAdvance(t, p2, ok, 2) // base = setup's weight (2)

	p3, ok := tr.HandleLine("@@PHASE:prebuild:Prebuilding")
	mustAdvance(t, p3, ok, 17) // base = setup+install = 2+15

	p4, ok := tr.HandleLine("@@PHASE:gradle:Running gradle")
	mustAdvance(t, p4, ok, 30) // base = setup+install+prebuild+signing = 2+15+10+3
}

func TestProgressMarkerBlendsWithinCurrentPhaseBudget(t *testing.T) {
	var tr Tracker
	tr.SetEngine("gradle")
	tr.HandleLine("@@PHASE:install:Installing") // base=2, weight=15
	half, ok := tr.HandleLine("@@PROGRESS:50")
	mustAdvance(t, half, ok, 10) // 2 + round(15*0.5) = 2 + round(7.5) = 2 + 8 = 10

	full, ok := tr.HandleLine("@@PROGRESS:100")
	mustAdvance(t, full, ok, 17) // 2 + 15
}

func TestGradleConsoleRichLineAdvancesWithinGradlePhase(t *testing.T) {
	var tr Tracker
	tr.SetEngine("gradle")
	tr.HandleLine("@@PHASE:gradle:Running gradle") // base = 30, weight = 65
	pct, ok := tr.HandleLine("<=====-----> 50% EXECUTING")
	mustAdvance(t, pct, ok, 63) // 30 + round(65*0.5) = 30 + round(32.5) = 30 + 33 = 63

	pct2, ok := tr.HandleLine("<==========> 90% EXECUTING")
	mustAdvance(t, pct2, ok, 89) // 30 + round(65*0.9) = 30 + round(58.5) = 30 + 59 = 89
}

func TestGradleRegexIgnoredOutsideTheGradlePhase(t *testing.T) {
	var tr Tracker
	tr.SetEngine("gradle")
	tr.HandleLine("@@PHASE:install:Installing")
	_, ok := tr.HandleLine("<=====-----> 50% EXECUTING")
	if ok {
		t.Error("expected no progress update outside the gradle phase")
	}
}

func TestEasMilestonesStepForwardInOrder(t *testing.T) {
	var tr Tracker
	tr.SetEngine("eas")
	// EAS weights: setup=2, install=15, prebuild=0, signing=3, gradle=0, eas=75, ...
	tr.HandleLine("@@PHASE:eas:Running eas build") // base = 2+15+0+3 = 20, weight = 75

	m1, ok := tr.HandleLine("Resolving credentials for build")
	mustAdvance(t, m1, ok, 28) // 20 + round(75*0.1) = 20 + round(7.5) = 20 + 8 = 28

	m2, ok := tr.HandleLine("Compressing project files")
	mustAdvance(t, m2, ok, 35) // 20 + round(75*0.2) = 20 + 15 = 35

	m3, ok := tr.HandleLine("BUILD SUCCESSFUL in 3m")
	mustAdvance(t, m3, ok, 91) // 20 + round(75*0.95) = 20 + round(71.25) = 20 + 71 = 91
}

func TestEasMilestonesNeverGoBackwardEvenIfALineMatchesAnEarlierOneAgain(t *testing.T) {
	var tr Tracker
	tr.SetEngine("eas")
	tr.HandleLine("@@PHASE:eas:Running eas build")
	m1, ok := tr.HandleLine("Compressing project files")
	if !ok {
		t.Fatal("expected a value")
	}
	afterCompress := m1

	// The tracker only scans forward from the last matched milestone index, so
	// an earlier milestone string appearing again (e.g. in unrelated log
	// noise) doesn't match and doesn't move the percent backward.
	if _, ok := tr.HandleLine("Resolving credentials again for retry"); ok {
		t.Error("expected no match for an earlier milestone appearing again")
	}

	m3, ok := tr.HandleLine("Installing node_modules")
	if !ok {
		t.Fatal("expected a value")
	}
	if m3 < afterCompress {
		t.Errorf("m3 = %d, want >= %d", m3, afterCompress)
	}
}

func TestPercentNeverDecreasesEvenIfALaterPhaseMarkerImpliesLess(t *testing.T) {
	var tr Tracker
	tr.SetEngine("gradle")
	tr.HandleLine("@@PHASE:gradle:Running gradle") // base = 30
	tr.HandleLine("<==========> 90% EXECUTING")    // advances well past 30

	// A malformed/out-of-order @@PROGRESS: within the same phase must not pull
	// the overall percent back down.
	after, ok := tr.HandleLine("@@PROGRESS:0")
	if !ok {
		t.Fatal("expected a value")
	}
	if after < 30+58 {
		t.Errorf("after = %d, want >= %d", after, 30+58)
	}
}

func TestUnrelatedLogLinesReturnNoValue(t *testing.T) {
	var tr Tracker
	tr.SetEngine("gradle")
	tr.HandleLine("@@PHASE:setup:Starting")
	if _, ok := tr.HandleLine("some ordinary build tool output line"); ok {
		t.Error("expected no value for an unrelated log line")
	}
}

func TestUnrecognizedPhaseIdIsIgnoredRatherThanGuessed(t *testing.T) {
	var tr Tracker
	tr.SetEngine("gradle")
	if _, ok := tr.HandleLine("@@PHASE:not-a-real-phase:Whatever"); ok {
		t.Error("expected no value for an unrecognized phase id")
	}
}

func TestMalformedProgressValueIsIgnored(t *testing.T) {
	var tr Tracker
	tr.SetEngine("gradle")
	tr.HandleLine("@@PHASE:install:Installing")
	if _, ok := tr.HandleLine("@@PROGRESS:not-a-number"); ok {
		t.Error("expected no value for a malformed progress value")
	}
}

func TestDefaultEngineIsGradleWhenSetEngineNeverCalled(t *testing.T) {
	var tr Tracker
	if _, ok := tr.HandleLine("@@PHASE:gradle:Running gradle"); !ok {
		t.Fatal("expected a value")
	}
	if _, ok := tr.HandleLine("<=====-----> 20% EXECUTING"); !ok {
		t.Fatal("expected a value")
	}
}

func TestUnknownEngineStringFallsBackToGradle(t *testing.T) {
	var tr Tracker
	tr.SetEngine("something-else")
	tr.HandleLine("@@PHASE:eas:Running eas") // gradle weight for "eas" phase index is 0
	pct, ok := tr.HandleLine("@@PROGRESS:100")
	if !ok {
		t.Fatal("expected a value")
	}
	// Gradle's eas-phase weight is 0, so 100% within it adds nothing new.
	want := 2 + 15 + 10 + 3 + 65 // base for phase after gradle = sum of all gradle weights before eas
	if pct != want {
		t.Errorf("pct = %d, want %d", pct, want)
	}
}
