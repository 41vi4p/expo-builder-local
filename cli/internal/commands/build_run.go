package commands

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"os"
	"os/signal"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/41vi4p/expo-builder-local/cli/internal/buildstatusview"
	"github.com/41vi4p/expo-builder-local/cli/internal/color"
	"github.com/41vi4p/expo-builder-local/cli/internal/detect"
	"github.com/41vi4p/expo-builder-local/cli/internal/dockerapi"
	"github.com/41vi4p/expo-builder-local/cli/internal/metrics"
	"github.com/41vi4p/expo-builder-local/cli/internal/progress"
)

const (
	recentLogLineCap = 8
	fullLogLineCap   = 200
)

// runBuildContainer is the direct port of runBuild's try{} block in
// cli/src/commands/build.cpp (lines ~639-963): the actual Docker
// orchestration, once every option has been resolved and validated.
func runBuildContainer(opts buildOptions, params dockerapi.BuildParams, appPath, profile, artifact, runnerImage string,
	project detect.ProjectInfo, wantDashboard bool, log io.Writer, logFile *os.File) int {
	ctx := context.Background()
	docker := dockerapi.New(opts.dockerSocket)

	// Clear screen immediately if dashboard mode - do this before any output so
	// there's no clutter from earlier phases (docker pull, ensureRunnerImage, etc)
	if wantDashboard {
		fmt.Print("\x1b[2J\x1b[H")
	}

	fail := func(message string) int {
		if opts.jsonOut {
			printJSON(earlyFailJSON{Success: false, Error: message})
		} else {
			fmt.Fprintln(os.Stderr, color.Red(message))
		}
		return 1
	}

	// Two `ebl build`s of the same project at once share the same
	// npm/Gradle cache volumes and end up contending on npm's own cache
	// lock - neither one makes any real progress rather than failing
	// cleanly. Fail fast instead of launching a second container that would
	// just wedge alongside the first.
	if existingID, found, err := docker.FindRunningBuildContainerByAppPath(ctx, appPath); err == nil && found {
		short := existingID
		if len(short) > 12 {
			short = short[:12]
		}
		return fail(fmt.Sprintf("A build for %s is already running (container %s). Wait for it to finish, or stop it with: docker stop %s", appPath, short, existingID))
	}

	if err := ensureRunnerImage(ctx, docker, runnerImage, log, logFile); err != nil {
		return fail(err.Error())
	}
	if err := docker.EnsureVolume(ctx, opts.gradleCacheVolume); err != nil {
		return fail(err.Error())
	}
	if err := docker.EnsureVolume(ctx, opts.npmCacheVolume); err != nil {
		return fail(err.Error())
	}

	// Only print initial status in non-dashboard modes - dashboard will render these
	if !wantDashboard {
		fmt.Fprintln(log)
		fmt.Fprintln(log, color.Bold("Building "+color.Cyan(appPath)))
		fmt.Fprintln(log, color.Dim("  profile="+profile+" artifact="+artifact+" engine="+opts.engine+" signing="+params.SigningMode))
		fmt.Fprintln(log)
	}

	buildUID, buildGID := buildUIDGID()
	containerID, err := docker.CreateContainer(ctx, params, runnerImage, opts.gradleCacheVolume, opts.npmCacheVolume, buildUID, buildGID)
	if err != nil {
		return fail(err.Error())
	}

	// Only print container info in non-dashboard modes
	if !wantDashboard {
		fmt.Fprintln(log, color.Dim("Container: "+containerID))
		fmt.Fprintln(log, color.Dim("Press Ctrl-C to cancel - the container will be stopped and removed."))
	} else {
		// Clear screen again right before dashboard starts - ensures a clean slate
		// after ensureRunnerImage (docker pull) has printed many lines of output
		fmt.Print("\x1b[2J\x1b[H")
	}

	// Ctrl-C (SIGINT) or a `kill` (SIGTERM) is watched here; this goroutine
	// notices it and force-removes the container, which is what unblocks
	// the main goroutine's WaitContainer below. Without this, interrupting
	// `ebl build` would kill this process but leave the container running
	// indefinitely - which is exactly how orphaned, mutually-wedging build
	// containers piled up in practice before this existed.
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)
	buildFinished := make(chan struct{})
	cancelDone := make(chan struct{})
	wasCancelled := false
	go func() {
		defer close(cancelDone)
		select {
		case <-sigChan:
			wasCancelled = true
			fmt.Fprintln(os.Stderr)
			fmt.Fprintln(os.Stderr, color.Yellow("Cancelling - stopping and removing the build container..."))
			killCtx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
			defer cancel()
			if err := docker.RemoveContainer(killCtx, containerID); err != nil {
				fmt.Fprintln(os.Stderr, color.Red("Failed to remove container: "+err.Error()))
				fmt.Fprintln(os.Stderr, color.Dim("Clean it up manually with: docker rm -f "+containerID))
			} else {
				fmt.Fprintln(os.Stderr, color.Green("Container stopped and removed."))
			}
		case <-buildFinished:
		}
	}()

	var resolvedEngine, artifactPath, errorMessage, buildNumber string
	var residual bytes.Buffer

	// Dashboard-only state: onChunk (running on the attach goroutine) is the
	// sole writer; statusMutex protects statusState specifically because the
	// status-polling goroutine below also writes to it (CPU/memory samples)
	// and reads it (to render). fullLogBuffer needs no lock - only ever
	// touched here, and only ever read after the attach goroutine has
	// already finished (see the failure path below).
	var statusMutex sync.Mutex
	statusState := buildstatusview.NewState()
	var fullLogBuffer []string
	var progressTracker progress.Tracker

	// Line-buffered rather than byte-immediate: every line is scanned for
	// ebl's own @@-prefixed markers regardless of mode, so --logs's raw
	// passthrough skips printing them (never leaking through as literal
	// text) instead of writing raw bytes straight through before they're
	// even parsed.
	onChunk := func(chunk []byte) {
		residual.Write(chunk)
		for {
			b := residual.Bytes()
			idx := bytes.IndexAny(b, "\r\n")
			if idx < 0 {
				break
			}
			line := string(b[:idx])
			residual.Next(idx + 1)

			isMarker := strings.HasPrefix(line, "@@")

			switch {
			case strings.HasPrefix(line, "@@ENGINE:"):
				resolvedEngine = line[len("@@ENGINE:"):]
				progressTracker.SetEngine(resolvedEngine)
			case strings.HasPrefix(line, "@@ARTIFACT:"):
				artifactPath = toHostArtifactPath(params.AppPath, line[len("@@ARTIFACT:"):])
			case strings.HasPrefix(line, "@@ERROR:"):
				errorMessage = line[len("@@ERROR:"):]
			case strings.HasPrefix(line, "@@BUILD_NUMBER:"):
				buildNumber = line[len("@@BUILD_NUMBER:"):]
			case strings.HasPrefix(line, "@@PHASE:"):
				// Format: @@PHASE:<id>:<label> - split on the first colon
				// only, in case a label itself ever contains one. Just the
				// display text here - the percent itself comes from
				// progressTracker below, fed this same line.
				if wantDashboard {
					rest := line[len("@@PHASE:"):]
					phaseID, phaseLabel := rest, ""
					if sep := strings.IndexByte(rest, ':'); sep != -1 {
						phaseID, phaseLabel = rest[:sep], rest[sep+1:]
					}
					statusMutex.Lock()
					statusState.PhaseID = phaseID
					statusState.PhaseLabel = phaseLabel
					statusMutex.Unlock()
				}
			}

			// Feed every line (marker or not) through the progress tracker -
			// its Gradle/EAS live-output regex matching needs to see real
			// build-tool output, not just @@PROGRESS: markers.
			if wantDashboard {
				if pct, ok := progressTracker.HandleLine(line); ok {
					statusMutex.Lock()
					statusState.ProgressPercent = pct
					statusMutex.Unlock()
				}
			}

			if isMarker {
				continue // never shown as content, dashboard or --logs
			}

			if wantDashboard {
				// Stripped, not raw: a spinner's own cursor-reposition/erase
				// codes would otherwise execute for real inside our
				// redraw-in-place frame - a burst of pure spinner noise
				// strips down to nothing and is skipped rather than shown
				// as a blank line.
				clean := strings.TrimSpace(stripAnsiEscapes(line))
				if clean != "" {
					statusMutex.Lock()
					statusState.RecentLogLines = append(statusState.RecentLogLines, clean)
					if len(statusState.RecentLogLines) > recentLogLineCap {
						statusState.RecentLogLines = statusState.RecentLogLines[1:]
					}
					statusMutex.Unlock()

					fullLogBuffer = append(fullLogBuffer, clean)
					if len(fullLogBuffer) > fullLogLineCap {
						fullLogBuffer = fullLogBuffer[1:]
					}
				}
			} else {
				fmt.Fprintln(log, line)
			}
		}
	}

	// AttachAndStream blocks until the container's output stream closes,
	// which only happens once the container exits - so it has to run on its
	// own goroutine. The main goroutine then starts the container and waits
	// for it, and "joins" (reads from attachErrChan) afterward to make sure
	// every last buffered chunk of output has been flushed before printing
	// the summary.
	attachErrChan := make(chan error, 1)
	go func() {
		attachErrChan <- docker.AttachAndStream(context.Background(), containerID, onChunk)
	}()

	startedAt := time.Now()

	// Polls the container's CPU/memory once a second and redraws the
	// dashboard - a single goroutine does both, rather than splitting
	// polling and rendering, to avoid an extra goroutine + lock for no real
	// benefit at a 1Hz cadence. Started before StartContainer below (same
	// reasoning as the attach/cancel goroutines being created before it) -
	// a stats fetch against a not-yet-running container just errors, caught
	// per-tick, and that tick is skipped.
	statusView := buildstatusview.New(appPath)
	statusDone := make(chan struct{})
	if wantDashboard {
		go func() {
			defer close(statusDone)
			for {
				select {
				case <-buildFinished:
					return
				default:
				}

				if stats, err := docker.GetContainerStats(context.Background(), containerID); err == nil {
					statusMutex.Lock()
					statusState.CPUPercent = stats.CPUPercent
					statusState.MemUsedMB = stats.MemUsedMB
					statusState.MemLimitMB = stats.MemLimitMB
					statusMutex.Unlock()
				}
				// One missed sample isn't fatal - the dashboard just keeps
				// showing the last known values until the next tick
				// succeeds.

				statusMutex.Lock()
				snapshot := statusState
				snapshot.RecentLogLines = append([]string(nil), statusState.RecentLogLines...)
				statusMutex.Unlock()
				snapshot.ElapsedSeconds = int64(time.Since(startedAt).Seconds())
				statusView.Render(snapshot)

				time.Sleep(time.Second)
			}
		}()
	} else {
		close(statusDone)
	}

	if err := docker.StartContainer(ctx, containerID); err != nil {
		return fail(err.Error())
	}
	exitStatus, waitErr := docker.WaitContainer(ctx, containerID)
	durationSeconds := int64(time.Since(startedAt).Seconds())

	attachErr := <-attachErrChan

	if wantDashboard {
		// One last render with the truly final state (the attach goroutine
		// has now drained every marker, including @@PHASE:done/
		// @@PROGRESS:100) - the status goroutine's own last tick could
		// otherwise have raced ahead of the final markers and left a
		// slightly stale frame as the last thing visible before the
		// success/failure summary below it.
		statusMutex.Lock()
		finalSnapshot := statusState
		finalSnapshot.RecentLogLines = append([]string(nil), statusState.RecentLogLines...)
		statusMutex.Unlock()
		finalSnapshot.ElapsedSeconds = durationSeconds
		statusView.Render(finalSnapshot)
	}

	close(buildFinished)
	<-cancelDone
	<-statusDone
	signal.Stop(sigChan)

	if attachErr != nil {
		fmt.Fprintln(os.Stderr)
		fmt.Fprintln(os.Stderr, color.Yellow("Warning: log streaming ended early: "+attachErr.Error()))
	}

	if wasCancelled {
		if opts.jsonOut {
			printJSON(cancelledJSON{Success: false, Cancelled: true, DurationSeconds: durationSeconds})
		} else {
			fmt.Fprintln(log)
			fmt.Fprintln(log, color.Yellow(color.Bold("Build cancelled after "+formatBuildDuration(durationSeconds))))
			fmt.Fprintln(log)
		}
		return 130 // 128 + SIGINT, standard shell convention
	}

	if err := docker.RemoveContainer(ctx, containerID); err != nil {
		return fail(err.Error())
	}

	if waitErr != nil {
		return fail(waitErr.Error())
	}

	if exitStatus == 0 && artifactPath != "" {
		m, err := metrics.Extract(params.AppPath, artifactPath)
		if err != nil {
			return fail(err.Error())
		}
		engine := opts.engine
		if resolvedEngine != "" {
			engine = resolvedEngine
		}
		if opts.jsonOut {
			printJSON(buildSuccessJSON{
				Success:         true,
				ArtifactPath:    artifactPath,
				SizeBytes:       float64(m.SizeBytes),
				VersionName:     m.VersionName,
				VersionCode:     m.VersionCode,
				ApplicationID:   m.ApplicationID,
				Engine:          engine,
				BuildNumber:     buildNumber,
				DurationSeconds: durationSeconds,
				GitCommit:       m.GitCommit,
				GitBranch:       m.GitBranch,
				SHA256:          m.SHA256,
			})
		} else {
			title := "Build "
			if buildNumber != "" {
				title += "#" + buildNumber + " "
			}
			title += "succeeded in " + formatBuildDuration(durationSeconds)
			fmt.Fprintln(log)
			fmt.Fprintln(log, color.Green(color.Bold(title)))
			fmt.Fprintln(log, "  "+color.Dim("Artifact:")+"     "+artifactPath)
			fmt.Fprintln(log, "  "+color.Dim("Size:")+"         "+formatBytes(m.SizeBytes))
			versionName := m.VersionName
			if versionName == "" {
				versionName = "?"
			}
			versionLine := "  " + color.Dim("Version:") + "      " + versionName
			if m.VersionCode != "" {
				versionLine += " (versionCode " + m.VersionCode + ")"
			}
			fmt.Fprintln(log, versionLine)
			if m.ApplicationID != "" {
				fmt.Fprintln(log, "  "+color.Dim("Application:")+"  "+m.ApplicationID)
			}
			fmt.Fprintln(log, "  "+color.Dim("Engine:")+"       "+engine)
			if m.GitCommit != "" {
				fmt.Fprintln(log, "  "+color.Dim("Git:")+"          "+m.GitBranch+"@"+m.GitCommit)
			}
			fmt.Fprintln(log, "  "+color.Dim("SHA-256:")+"      "+m.SHA256)
			fmt.Fprintln(log)
		}
		return 0
	}

	finalError := errorMessage
	if finalError == "" {
		finalError = fmt.Sprintf("Build process exited with status %d", exitStatus)
	}
	if opts.jsonOut {
		printJSON(buildFailJSON{Success: false, Error: finalError, DurationSeconds: durationSeconds})
	} else {
		fmt.Fprintln(log)
		fmt.Fprintln(log, color.Red(color.Bold("Build failed after "+formatBuildDuration(durationSeconds))))
		fmt.Fprintln(log, "  "+finalError)
		fmt.Fprintln(log)
		// The dashboard never shows the raw build log at all (that's the
		// whole point) - on failure specifically, dump what was buffered so
		// a failure doesn't leave the user with nothing to diagnose it
		// from. Not needed on the success path (nothing to debug) or in
		// --json mode (which doesn't show a dashboard either, so there's
		// nothing buffered to dump here).
		if wantDashboard && len(fullLogBuffer) > 0 {
			fmt.Fprintln(log, color.Dim(fmt.Sprintf("Last %d build log line(s):", len(fullLogBuffer))))
			for _, l := range fullLogBuffer {
				fmt.Fprintln(log, "  "+l)
			}
			fmt.Fprintln(log)
		}
	}
	return 1
}
