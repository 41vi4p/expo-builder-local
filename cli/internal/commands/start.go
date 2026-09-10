package commands

import (
	"context"
	"fmt"
	"net/http"
	"os"
	"strconv"
	"time"

	"github.com/41vi4p/expo-builder-local/cli/internal/color"
	"github.com/41vi4p/expo-builder-local/cli/internal/config"
	"github.com/41vi4p/expo-builder-local/cli/internal/dockerapi"
	"github.com/41vi4p/expo-builder-local/cli/internal/prompt"
	"github.com/41vi4p/expo-builder-local/cli/internal/pullprogress"
	"github.com/41vi4p/expo-builder-local/cli/internal/winpath"
)

const (
	serviceNetworkName     = "ebl-network"
	orchestratorContainer  = "ebl-orchestrator"
	webContainer           = "ebl-web"
	orchestratorDataVolume = "ebl-orchestrator-data"
	serviceGradleCacheVol  = "expo-builder-local_gradle-cache"
	serviceNpmCacheVol     = "expo-builder-local_npm-cache"
)

const startUsage = `ebl start

Starts the orchestrator + web GUI as Docker containers (pulling their images if
needed, or prompting to check for a newer one if already cached) using the settings
saved by ` + "`ebl config`" + `. No git checkout or docker-compose.yml required - this drives
the containers directly.

Options:
  -h, --help   Show this help
`

const stopUsage = `ebl stop

Stops and removes the orchestrator + web GUI containers started by ` + "`ebl start`" + `.
Build history/keystores are preserved (they live in a separate Docker volume).

Options:
  -h, --help   Show this help
`

// PrintStartUsage prints `ebl start`'s help text.
func PrintStartUsage() { fmt.Print(startUsage) }

// PrintStopUsage prints `ebl stop`'s help text.
func PrintStopUsage() { fmt.Print(stopUsage) }

// ensureServiceImage pulls an image if it isn't already present locally - no
// prompt, nothing running yet to disrupt. Unlike the runner image, there's
// no local-build fallback here - the orchestrator/web images are meant to
// be pre-built and published; before they're published, build them from
// this repo checkout (`docker compose build`) so they exist locally for
// `ebl start` to find.
//
// If a cached image is already present, asks first instead of pulling
// unconditionally: `ebl start` always tears down and recreates its
// containers (CreateServiceContainer removes any same-named container
// before creating), so a newer image takes effect immediately on this same
// run - worth confirming before spending the time/bandwidth on every single
// `ebl start`, unlike `ebl build`'s always-pull (a fresh disposable
// container either way, nothing to disrupt). Defaults to yes on Enter, so
// scripted use still gets checked by default.
func ensureServiceImage(ctx context.Context, docker *dockerapi.Client, tag, friendlyName string) error {
	cachedLocally, _ := docker.ImageExists(ctx, tag)
	if cachedLocally {
		answer := prompt.String("Check for a newer "+friendlyName+" image ("+tag+")?", "y")
		if answer != "" && answer[0] != 'y' && answer[0] != 'Y' {
			fmt.Println(color.Dim("Skipping update check - using the cached " + friendlyName + " image."))
			return nil
		}
	}

	verb := "Pulling"
	if cachedLocally {
		verb = "Checking"
	}
	fmt.Println(color.Dim(verb + " " + friendlyName + " image (" + tag + ")..."))
	progress := pullprogress.New(os.Stdout)
	err := docker.PullImage(ctx, tag, func(e dockerapi.PullEvent) {
		progress.OnEvent(e.ID, e.Status, e.Progress)
	})
	if err == nil {
		return nil
	}
	// A cached image to fall back to turns a failed check into a soft
	// warning; with no cache yet (first-time pull), the same failure is
	// fatal - same as before this function could prompt at all - so
	// propagate it and let RunStart's caller report it.
	if !cachedLocally {
		return err
	}
	fmt.Println(color.Dim("Update check failed (" + err.Error() + ") - using the cached " + friendlyName + " image."))
	return nil
}

func waitForHealth(url string, attempts int, delay time.Duration) bool {
	client := http.Client{Timeout: 3 * time.Second}
	for i := 0; i < attempts; i++ {
		resp, err := client.Get(url)
		if err == nil {
			resp.Body.Close()
			if resp.StatusCode == 200 {
				return true
			}
		}
		time.Sleep(delay)
	}
	return false
}

// RunStart implements `ebl start [options]`. args are the arguments after
// the "start" subcommand token. Returns the process exit code.
func RunStart(args []string) int {
	for _, arg := range args {
		if arg == "-h" || arg == "--help" {
			PrintStartUsage()
			return 0
		}
	}

	cfg, found, err := config.Load()
	if err != nil || !found {
		fmt.Fprintln(os.Stderr, color.Red("No configuration found.")+" Run "+color.Cyan("ebl config")+" first.")
		return 1
	}
	if cfg.ProjectsRoot == "" {
		fmt.Fprintln(os.Stderr, color.Red("No projects folder configured.")+" Run "+color.Cyan("ebl config")+" first.")
		return 1
	}

	ctx := context.Background()
	docker := dockerapi.New("/var/run/docker.sock")
	if !docker.Ping(ctx) {
		fmt.Fprintln(os.Stderr, color.Red("Docker isn't reachable.")+" Run "+color.Cyan("ebl setup")+" first.")
		return 1
	}

	fail := func(err error) int {
		fmt.Fprintln(os.Stderr, color.Red("Failed to start: "+err.Error()))
		return 1
	}

	fmt.Println(color.Bold("Preparing images..."))
	if err := ensureServiceImage(ctx, docker, cfg.OrchestratorImage(), "orchestrator"); err != nil {
		return fail(err)
	}
	if err := ensureServiceImage(ctx, docker, cfg.WebImage(), "web"); err != nil {
		return fail(err)
	}

	fmt.Println(color.Bold("Starting orchestrator..."))
	// Same path on both sides: the orchestrator talks to the *host* Docker
	// daemon over the mounted socket (a sibling container, not a nested
	// one), so any path it hands to the daemon for a build container's bind
	// mount must already be a real host path - not remapped inside this
	// container. winpath.ToDockerBindPath is the identity function on
	// non-Windows; on Windows it turns "D:\Projects" into "//d/Projects" - a
	// form Linux (inside the orchestrator container) still accepts as an
	// ordinary absolute mount destination, so the "same path both sides"
	// trick still holds. Note: this only covers the *native ebl.exe* side of
	// that bind string - the orchestrator itself (a separate Node/TS
	// codebase) must independently be Windows-path-aware for anything it
	// does beyond relaying this same string.
	projectsRootBind := winpath.ToDockerBindPath(cfg.ProjectsRoot)
	buildUID, buildGID := buildUIDGID()

	orchestratorEnv := []string{
		"PORT=4001",
		"HOST=0.0.0.0",
		"CORS_ORIGIN=*",
		"DATA_DIR=/data",
		"DOCKER_SOCKET=/var/run/docker.sock",
		"RUNNER_IMAGE=" + cfg.RunnerImage(),
		"GRADLE_CACHE_VOLUME=" + serviceGradleCacheVol,
		"NPM_CACHE_VOLUME=" + serviceNpmCacheVol,
		// Matches projectsRootBind above (the path the orchestrator actually
		// sees inside its own container), not the raw un-translated
		// cfg.ProjectsRoot.
		"ALLOWED_ROOTS=" + projectsRootBind,
		fmt.Sprintf("HOST_UID=%d", buildUID),
		fmt.Sprintf("HOST_GID=%d", buildGID),
		"MASTER_KEY=" + cfg.MasterKey,
		"MAX_CONCURRENT_BUILDS=1",
	}
	if cfg.ExpoToken != "" {
		orchestratorEnv = append(orchestratorEnv, "EXPO_TOKEN="+cfg.ExpoToken)
	}

	orchestratorSpec := dockerapi.ServiceContainerSpec{
		Name:    orchestratorContainer,
		Image:   cfg.OrchestratorImage(),
		Network: serviceNetworkName,
		PortBindings: []dockerapi.PortBinding{
			{ContainerPort: "4001/tcp", HostPort: strconv.Itoa(cfg.OrchestratorPort)},
		},
		Binds: []string{
			"/var/run/docker.sock:/var/run/docker.sock",
			projectsRootBind + ":" + projectsRootBind,
			orchestratorDataVolume + ":/data",
		},
		Env: orchestratorEnv,
	}

	orchestratorID, err := docker.CreateServiceContainer(ctx, orchestratorSpec)
	if err != nil {
		return fail(err)
	}
	if err := docker.StartContainer(ctx, orchestratorID); err != nil {
		return fail(err)
	}

	fmt.Println(color.Bold("Starting web GUI..."))
	webSpec := dockerapi.ServiceContainerSpec{
		Name:    webContainer,
		Image:   cfg.WebImage(),
		Network: serviceNetworkName,
		PortBindings: []dockerapi.PortBinding{
			{ContainerPort: "3000/tcp", HostPort: strconv.Itoa(cfg.WebPort)},
		},
		Env: []string{fmt.Sprintf("ORCHESTRATOR_URL=http://localhost:%d", cfg.OrchestratorPort)},
	}

	webID, err := docker.CreateServiceContainer(ctx, webSpec)
	if err != nil {
		return fail(err)
	}
	if err := docker.StartContainer(ctx, webID); err != nil {
		return fail(err)
	}

	orchestratorURL := fmt.Sprintf("http://localhost:%d", cfg.OrchestratorPort)
	webURL := fmt.Sprintf("http://localhost:%d", cfg.WebPort)

	fmt.Println(color.Dim("Waiting for the orchestrator to come online..."))
	orchestratorUp := waitForHealth(orchestratorURL+"/api/health", 30, time.Second)
	webUp := waitForHealth(webURL, 30, time.Second)

	fmt.Println()
	orchestratorStatus := color.Red("[not responding]")
	if orchestratorUp {
		orchestratorStatus = color.Green("[online]")
	}
	fmt.Println("  Orchestrator: " + orchestratorURL + "  " + orchestratorStatus)
	webStatus := color.Red("[not responding]")
	if webUp {
		webStatus = color.Green("[online]")
	}
	fmt.Println("  Web GUI:      " + webURL + "  " + webStatus)
	fmt.Println()

	if !orchestratorUp || !webUp {
		fmt.Println(color.Yellow("One or more services didn't respond in time. Check `docker logs " +
			orchestratorContainer + "` / `docker logs " + webContainer + "`."))
		return 1
	}

	fmt.Println(color.Green(color.Bold("Everything's up.")) + " Open " + color.Cyan(webURL) +
		" to use the GUI, or run " + color.Cyan("ebl build .") + " from a project folder.")
	return 0
}

// RunStop implements `ebl stop [options]`. args are the arguments after the
// "stop" subcommand token. Returns the process exit code.
func RunStop(args []string) int {
	for _, arg := range args {
		if arg == "-h" || arg == "--help" {
			PrintStopUsage()
			return 0
		}
	}

	ctx := context.Background()
	docker := dockerapi.New("/var/run/docker.sock")
	if !docker.Ping(ctx) {
		fmt.Fprintln(os.Stderr, color.Red("Docker isn't reachable."))
		return 1
	}

	if err := docker.RemoveContainerByName(ctx, webContainer); err != nil {
		fmt.Fprintln(os.Stderr, color.Red("Failed to stop: "+err.Error()))
		return 1
	}
	if err := docker.RemoveContainerByName(ctx, orchestratorContainer); err != nil {
		fmt.Fprintln(os.Stderr, color.Red("Failed to stop: "+err.Error()))
		return 1
	}
	fmt.Println(color.Green("Stopped.") + " (Build history and keystores are preserved.)")
	return 0
}
