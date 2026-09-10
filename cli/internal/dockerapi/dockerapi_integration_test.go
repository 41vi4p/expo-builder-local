package dockerapi

import (
	"context"
	"encoding/json"
	"net/http"
	"os"
	"path/filepath"
	"testing"
	"time"
)

// These tests exercise the real Docker Engine API against whatever daemon is
// reachable at /var/run/docker.sock - skipped automatically if none is
// available, so `go test ./...` still passes in environments without Docker
// (mirroring how this project's C++ ctest suite never touches a real Docker
// daemon either, and instead relies on this kind of hands-on verification
// during development - see docs/CHANGELOG.md for this session's history of
// exactly that approach).
func requireDocker(t *testing.T) *Client {
	t.Helper()
	c := New("/var/run/docker.sock")
	if !c.Ping(context.Background()) {
		t.Skip("no Docker daemon reachable at /var/run/docker.sock - skipping integration test")
	}
	if os := c.DaemonOS(context.Background()); os != "" && os != "linux" {
		t.Skipf("Docker daemon is in %s-container mode - these tests need Linux images", os)
	}
	return c
}

func TestPingAgainstRealDaemon(t *testing.T) {
	c := requireDocker(t)
	if !c.Ping(context.Background()) {
		t.Error("expected Ping to succeed")
	}
}

func TestVolumeLifecycleAgainstRealDaemon(t *testing.T) {
	c := requireDocker(t)
	ctx := context.Background()
	const name = "ebl_go_test_volume"

	if err := c.EnsureVolume(ctx, name); err != nil {
		t.Fatalf("EnsureVolume: %v", err)
	}
	// Idempotent - a second call must not error.
	if err := c.EnsureVolume(ctx, name); err != nil {
		t.Fatalf("EnsureVolume (2nd call): %v", err)
	}
	if err := c.RemoveVolume(ctx, name); err != nil {
		t.Fatalf("RemoveVolume: %v", err)
	}
	// Removing an already-gone volume is a no-op, not an error.
	if err := c.RemoveVolume(ctx, name); err != nil {
		t.Fatalf("RemoveVolume (already gone): %v", err)
	}
}

// pullTestImage is a small, stable, rarely-changing image used across these
// tests so they don't each pay their own pull cost.
const pullTestImage = "alpine:3.20"

func ensureTestImage(t *testing.T, c *Client) {
	t.Helper()
	ctx := context.Background()
	exists, err := c.ImageExists(ctx, pullTestImage)
	if err != nil {
		t.Fatalf("ImageExists: %v", err)
	}
	if exists {
		return
	}
	var sawStatus bool
	err = c.PullImage(ctx, pullTestImage, func(e PullEvent) {
		if e.Status != "" {
			sawStatus = true
		}
	})
	if err != nil {
		t.Fatalf("PullImage: %v", err)
	}
	if !sawStatus {
		t.Error("expected at least one status event from PullImage")
	}
}

func TestPullAndImageExistsAgainstRealDaemon(t *testing.T) {
	c := requireDocker(t)
	ensureTestImage(t, c)

	exists, err := c.ImageExists(context.Background(), pullTestImage)
	if err != nil {
		t.Fatalf("ImageExists: %v", err)
	}
	if !exists {
		t.Error("expected the pulled image to exist")
	}
}

// createQuickContainer bypasses the public CreateContainer (which - matching
// the C++ CLI's own build-only design - always runs the image's own default
// ENTRYPOINT/CMD unmodified, since that's correct for the real runner image
// but would hang forever on a generic test image like alpine, whose default
// CMD is an interactive shell with nothing to make it exit) and instead
// creates a container with an explicit Cmd that exits immediately, using the
// package's own low-level `do` so this test can exercise
// start/attach/wait/remove end to end without hanging.
func createQuickContainer(t *testing.T, c *Client, appPath string) string {
	t.Helper()
	body, _ := json.Marshal(map[string]any{
		"Image":  pullTestImage,
		"Cmd":    []string{"/bin/sh", "-c", "echo hello-from-container; exit 7"},
		"Labels": map[string]string{AppPathLabel: appPath},
		"Tty":    true,
	})
	status, respBody, err := c.do(context.Background(), http.MethodPost, "/containers/create", body, map[string]string{"Content-Type": "application/json"}, defaultTimeout)
	if err != nil {
		t.Fatalf("create quick container: %v", err)
	}
	if status != 201 {
		t.Fatalf("create quick container: HTTP %d: %s", status, respBody)
	}
	var parsed struct {
		ID string `json:"Id"`
	}
	if err := json.Unmarshal(respBody, &parsed); err != nil {
		t.Fatal(err)
	}
	return parsed.ID
}

func TestContainerLifecycleAgainstRealDaemon(t *testing.T) {
	c := requireDocker(t)
	ensureTestImage(t, c)
	ctx := context.Background()
	const appPath = "/tmp/ebl-go-test-app-path"

	id := createQuickContainer(t, c, appPath)
	defer c.RemoveContainer(ctx, id)

	if _, found, err := c.FindRunningBuildContainerByAppPath(ctx, appPath); err != nil {
		t.Fatalf("FindRunningBuildContainerByAppPath (before start): %v", err)
	} else if found {
		t.Error("expected not found before the container starts")
	}

	// Attach *before* starting - for a container whose command finishes
	// almost instantly (as this test's does), starting first would race
	// attach's own connection setup against the container already exiting,
	// potentially missing all of its output. Real production code
	// (commands/build.go, once ported) follows this exact same ordering for
	// the exact same reason.
	var streamed []byte
	streamDone := make(chan error, 1)
	attachStarted := make(chan struct{})
	go func() {
		close(attachStarted)
		streamDone <- c.AttachAndStream(ctx, id, func(chunk []byte) {
			streamed = append(streamed, chunk...)
		})
	}()
	<-attachStarted
	time.Sleep(50 * time.Millisecond) // let the attach HTTP request actually reach the daemon

	if err := c.StartContainer(ctx, id); err != nil {
		t.Fatalf("StartContainer: %v", err)
	}

	exitCode, err := c.WaitContainer(ctx, id)
	if err != nil {
		t.Fatalf("WaitContainer: %v", err)
	}
	if exitCode != 7 {
		t.Errorf("exitCode = %d, want 7", exitCode)
	}

	select {
	case err := <-streamDone:
		if err != nil {
			t.Errorf("AttachAndStream: %v", err)
		}
	case <-time.After(10 * time.Second):
		t.Error("AttachAndStream did not return after the container exited")
	}
	if got := string(streamed); got == "" {
		t.Error("expected some streamed output from the container")
	} else {
		t.Logf("streamed output: %q", got)
	}

	// /containers/{id}/wait returning doesn't guarantee /containers/json's
	// state has flipped to "exited" in that exact instant (a small window
	// inherent in Docker's own API, not this client) - poll briefly rather
	// than asserting on the very first listing.
	var found bool
	var lastState string
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		containers, err := c.ListBuildContainers(ctx)
		if err != nil {
			t.Fatalf("ListBuildContainers: %v", err)
		}
		for _, ci := range containers {
			if ci.ID == id {
				found = true
				lastState = ci.State
			}
		}
		if lastState == "exited" {
			break
		}
		time.Sleep(100 * time.Millisecond)
	}
	if !found {
		t.Error("expected the exited container to show up in ListBuildContainers")
	}
	if lastState != "exited" {
		t.Errorf("container state = %q, want exited", lastState)
	}

	if err := c.RemoveContainer(ctx, id); err != nil {
		t.Fatalf("RemoveContainer: %v", err)
	}
	// Removing an already-gone container is a no-op, not an error.
	if err := c.RemoveContainer(ctx, id); err != nil {
		t.Fatalf("RemoveContainer (already gone): %v", err)
	}
}

func TestServiceContainerLifecycleAgainstRealDaemon(t *testing.T) {
	c := requireDocker(t)
	ensureTestImage(t, c)
	ctx := context.Background()
	const name = "ebl-go-test-service"
	const network = "ebl-go-test-network"

	id, err := c.CreateServiceContainer(ctx, ServiceContainerSpec{
		Name:    name,
		Image:   pullTestImage,
		Network: network,
	})
	if err != nil {
		t.Fatalf("CreateServiceContainer: %v", err)
	}
	defer func() {
		c.RemoveContainerByName(ctx, name)
	}()

	foundID, found, err := c.FindContainerIDByName(ctx, name)
	if err != nil {
		t.Fatalf("FindContainerIDByName: %v", err)
	}
	if !found || foundID != id {
		t.Errorf("FindContainerIDByName = %q, %v, want %q, true", foundID, found, id)
	}

	if c.IsContainerRunning(ctx, id) {
		t.Error("expected not running before start (never started in this test)")
	}

	// Creating again with the same name must remove the old one first rather
	// than erroring (fresh config on every `ebl start`).
	id2, err := c.CreateServiceContainer(ctx, ServiceContainerSpec{Name: name, Image: pullTestImage, Network: network})
	if err != nil {
		t.Fatalf("CreateServiceContainer (recreate): %v", err)
	}
	if id2 == id {
		t.Error("expected a genuinely new container id on recreate")
	}

	if err := c.RemoveContainerByName(ctx, name); err != nil {
		t.Fatalf("RemoveContainerByName: %v", err)
	}
	if _, found, err := c.FindContainerIDByName(ctx, name); err != nil {
		t.Fatalf("FindContainerIDByName (after remove): %v", err)
	} else if found {
		t.Error("expected not found after RemoveContainerByName")
	}
}

func TestBuildImageAgainstRealDaemon(t *testing.T) {
	c := requireDocker(t)
	ctx := context.Background()
	const tag = "ebl-go-test-build:latest"
	contextDir := t.TempDir()
	dockerfile := "FROM " + pullTestImage + "\nRUN echo \"built by ebl-go test\" > /marker.txt\n"
	if err := os.WriteFile(filepath.Join(contextDir, "Dockerfile"), []byte(dockerfile), 0o644); err != nil {
		t.Fatal(err)
	}

	var logLines []string
	err := c.BuildImage(ctx, contextDir, tag, func(line string) {
		logLines = append(logLines, line)
	}, false)
	if err != nil {
		t.Fatalf("BuildImage: %v", err)
	}
	if len(logLines) == 0 {
		t.Error("expected at least one log line from the build")
	}

	exists, err := c.ImageExists(ctx, tag)
	if err != nil {
		t.Fatalf("ImageExists: %v", err)
	}
	if !exists {
		t.Error("expected the built image to exist")
	}

	if err := c.RemoveImage(ctx, tag); err != nil {
		t.Fatalf("RemoveImage: %v", err)
	}
}
