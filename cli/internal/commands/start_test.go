package commands

import (
	"context"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/41vi4p/expo-builder-local/cli/internal/dockerapi"
	"github.com/41vi4p/expo-builder-local/cli/internal/prompt"
)

// requireDocker skips the test if no Docker daemon is reachable - same
// convention as internal/dockerapi's own test helper of the same name.
func requireDocker(t *testing.T) *dockerapi.Client {
	t.Helper()
	docker := dockerapi.New("/var/run/docker.sock")
	if !docker.Ping(context.Background()) {
		t.Skip("no Docker daemon reachable - skipping integration test")
	}
	return docker
}

func testContext(t *testing.T) context.Context {
	t.Helper()
	return context.Background()
}

// serviceTestImage is a small, stable, rarely-changing image used to
// exercise ensureServiceImage's pull/cached-prompt logic without needing
// the real (large, published) orchestrator/web images RunStart itself is
// hardcoded to use.
const serviceTestImage = "alpine:3.20"

func ensureServiceTestImageCached(t *testing.T, ctx context.Context, docker *dockerapi.Client) {
	t.Helper()
	if exists, err := docker.ImageExists(ctx, serviceTestImage); err == nil && exists {
		return
	}
	if err := docker.PullImage(ctx, serviceTestImage, func(dockerapi.PullEvent) {}); err != nil {
		t.Fatalf("pulling %s: %v", serviceTestImage, err)
	}
}

func TestStartHelpReturnsZero(t *testing.T) {
	silenceOutput(t)
	if code := RunStart([]string{"--help"}); code != 0 {
		t.Errorf("code = %d, want 0", code)
	}
}

func TestStopHelpReturnsZero(t *testing.T) {
	silenceOutput(t)
	if code := RunStop([]string{"--help"}); code != 0 {
		t.Errorf("code = %d, want 0", code)
	}
}

func TestStartFailsCleanlyWithNoSavedConfig(t *testing.T) {
	silenceOutput(t)
	t.Setenv("XDG_CONFIG_HOME", t.TempDir()) // guarantees no config.json exists
	if code := RunStart(nil); code != 1 {
		t.Errorf("code = %d, want 1", code)
	}
}

func TestWaitForHealthSucceedsOnA200(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(200)
	}))
	defer srv.Close()

	if !waitForHealth(srv.URL, 5, 10*time.Millisecond) {
		t.Error("expected waitForHealth to succeed against a real 200 response")
	}
}

func TestWaitForHealthFailsOnNon200(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(503)
	}))
	defer srv.Close()

	if waitForHealth(srv.URL, 3, 10*time.Millisecond) {
		t.Error("expected waitForHealth to fail against a 503 response")
	}
}

func TestWaitForHealthFailsWhenNothingIsListening(t *testing.T) {
	if waitForHealth("http://127.0.0.1:1", 2, 10*time.Millisecond) {
		t.Error("expected waitForHealth to fail against an unreachable address")
	}
}

func TestWaitForHealthRecoversAfterInitialFailures(t *testing.T) {
	attempt := 0
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		attempt++
		if attempt < 3 {
			w.WriteHeader(503)
			return
		}
		w.WriteHeader(200)
	}))
	defer srv.Close()

	if !waitForHealth(srv.URL, 10, 5*time.Millisecond) {
		t.Error("expected waitForHealth to eventually succeed once the server starts returning 200")
	}
}

func TestEnsureServiceImagePullsWhenNotCached(t *testing.T) {
	silenceOutput(t)
	docker := requireDocker(t)
	ctx := testContext(t)

	// Make sure it's not already cached, so this exercises the real pull path.
	docker.RemoveImage(ctx, serviceTestImage)

	if err := ensureServiceImage(ctx, docker, serviceTestImage, "test"); err != nil {
		t.Fatalf("ensureServiceImage: %v", err)
	}
	exists, err := docker.ImageExists(ctx, serviceTestImage)
	if err != nil || !exists {
		t.Errorf("expected %s to exist after ensureServiceImage, exists=%v err=%v", serviceTestImage, exists, err)
	}
}

func TestEnsureServiceImagePromptsWhenCachedAndSkipsOnNo(t *testing.T) {
	silenceOutput(t)
	docker := requireDocker(t)
	ctx := testContext(t)
	ensureServiceTestImageCached(t, ctx, docker) // make sure it's cached first

	restore := prompt.SetInputForTesting(strings.NewReader("n\n"))
	defer restore()

	if err := ensureServiceImage(ctx, docker, serviceTestImage, "test"); err != nil {
		t.Fatalf("ensureServiceImage: %v", err)
	}
	// No assertion beyond "didn't error" - answering "n" should just skip the
	// update check and keep using the cached image, which is already
	// confirmed present by ensureTestImage above.
}
