//go:build !windows

package commands

import (
	"context"
	"os"
	"os/exec"
	"syscall"
	"testing"
	"time"

	"github.com/41vi4p/expo-builder-local/cli/internal/dockerapi"
)

const fakeRunnerImageSlow = "ebl-go-fake-runner-slow:test"

// TestRunBuildCancellationRemovesTheContainer verifies the highest-risk
// untested path in the whole build command: Ctrl-C (SIGINT) mid-build must
// actually stop and remove the running container, not just kill the CLI
// process and leave it orphaned - the exact failure mode
// cli/src/commands/build.cpp's own comments describe as having happened in
// practice before this cancellation logic existed. Sends a real SIGINT to
// this test process (RunBuild installs its own signal.Notify handler, so
// this doesn't kill the test binary) partway through a deliberately slow
// fake build, then confirms both the exit code (130) and that the
// container is actually gone from Docker afterward.
func TestRunBuildCancellationRemovesTheContainer(t *testing.T) {
	docker := dockerapi.New("/var/run/docker.sock")
	if !docker.Ping(context.Background()) {
		t.Skip("no Docker daemon reachable - skipping build integration test")
	}
	if exists, err := docker.ImageExists(context.Background(), fakeRunnerImageSlow); err != nil || !exists {
		build := exec.Command("docker", "build", "-t", fakeRunnerImageSlow, "./testdata/fakerunnerslow")
		if out, err := build.CombinedOutput(); err != nil {
			t.Fatalf("building slow fake runner image: %v\n%s", err, out)
		}
	}
	projectDir := makeFakeExpoProject(t)

	codeChan := make(chan int, 1)
	var out capturedOutput
	done := make(chan struct{})
	go func() {
		defer close(done)
		out = captureStdoutStderr(t, func() {
			codeChan <- RunBuild([]string{
				projectDir,
				"--runner-image", fakeRunnerImageSlow,
				"--engine", "gradle",
				"--gradle-cache-volume", "ebl_go_test_gradle_cache",
				"--npm-cache-volume", "ebl_go_test_npm_cache",
				"--logs",
			})
		})
	}()

	// Poll for the container to actually exist and be running, rather than
	// guessing a fixed sleep - by the time it's running, RunBuild has
	// already called signal.Notify (that happens right after container
	// creation, before StartContainer), so the signal below is guaranteed
	// to be caught rather than racing the handler's registration.
	deadline := time.Now().Add(10 * time.Second)
	var foundRunning bool
	for time.Now().Before(deadline) {
		if _, found, err := docker.FindRunningBuildContainerByAppPath(context.Background(), projectDir); err == nil && found {
			foundRunning = true
			break
		}
		time.Sleep(100 * time.Millisecond)
	}
	if !foundRunning {
		t.Fatal("timed out waiting for the fake build container to start running")
	}
	time.Sleep(200 * time.Millisecond) // let signal.Notify's registration settle

	if err := syscall.Kill(os.Getpid(), syscall.SIGINT); err != nil {
		t.Fatalf("sending SIGINT to self: %v", err)
	}

	select {
	case <-done:
	case <-time.After(15 * time.Second):
		t.Fatal("RunBuild did not return within 15s of SIGINT - cancellation appears to have hung")
	}

	var code int
	select {
	case code = <-codeChan:
	default:
		t.Fatal("RunBuild goroutine finished without sending an exit code")
	}
	if code != 130 {
		t.Errorf("exit code = %d, want 130\nstdout: %q\nstderr: %q", code, out.stdout, out.stderr)
	}

	// Confirm no leftover container for *this test's* project app path
	// remains running - the whole point of the cancellation logic. Checked
	// by app-path label specifically (not "any running ebl container") so
	// this doesn't false-positive on an unrelated container some other
	// process/test happens to have running concurrently.
	if _, found, err := docker.FindRunningBuildContainerByAppPath(context.Background(), projectDir); err != nil {
		t.Fatalf("FindRunningBuildContainerByAppPath: %v", err)
	} else if found {
		t.Error("found a still-running leftover container for this test's project after cancellation")
	}
}
