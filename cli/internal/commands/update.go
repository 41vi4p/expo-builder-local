package commands

import (
	"context"
	"fmt"
	"os"

	"github.com/41vi4p/expo-builder-local/cli/internal/color"
	"github.com/41vi4p/expo-builder-local/cli/internal/config"
	"github.com/41vi4p/expo-builder-local/cli/internal/dockerapi"
	"github.com/41vi4p/expo-builder-local/cli/internal/pullprogress"
	"github.com/41vi4p/expo-builder-local/cli/internal/runnerctx"
)

const updateUsage = `ebl update [options]

Force-refreshes the runner/orchestrator/web images right now, unconditionally.

` + "`ebl build`/`ebl start`" + ` already pull on every run, but a plain pull only ever
transfers layers that actually changed upstream - it can't fix an image whose
published tag itself was built from a stale Docker layer cache (e.g. eas-cli
inside the runner image resolving "latest" once, then every later build/publish
of that same layer silently reusing that old resolution). ` + "`ebl update`" + ` is for
when you want to be certain right now: it always re-pulls all three images, and
for the runner image specifically, if pulling isn't possible at all (offline, or
a custom --runner-image that was never published), it rebuilds it from the
bundled context with Docker's build cache fully disabled (nocache + a forced
re-pull of the base image), not a normal cached build.

This doesn't remove the images it's replacing - Docker leaves the old, now-
untagged layers on disk as reclaimable space; run ` + "`ebl clean --all`" + ` (or ` + "`docker\nimage prune`" + `)
afterward if you want that space back.

Options:
      --runner-image <tag>        (default: 41vi4p/expo-builder-local-runner:latest)
      --orchestrator-image <tag>  (default: 41vi4p/expo-builder-local-orchestrator:latest)
      --web-image <tag>           (default: 41vi4p/expo-builder-local-web:latest)
      --docker-socket <path>      Docker socket path (default: /var/run/docker.sock;
                                  ignored on Windows)
  -h, --help                      Show this help
`

// PrintUpdateUsage prints `ebl update`'s help text.
func PrintUpdateUsage() { fmt.Print(updateUsage) }

func pullFresh(ctx context.Context, docker *dockerapi.Client, tag, friendlyName string) bool {
	fmt.Println(color.Dim("Pulling " + friendlyName + " image (" + tag + ")..."))
	progress := pullprogress.New(os.Stdout)
	err := docker.PullImage(ctx, tag, func(e dockerapi.PullEvent) {
		progress.OnEvent(e.ID, e.Status, e.Progress)
	})
	if err != nil {
		fmt.Println(color.Yellow("  Pull failed: " + err.Error()))
		return false
	}
	return true
}

// RunUpdate implements `ebl update [options]`. args are the arguments after
// the "update" subcommand token. Returns the process exit code.
func RunUpdate(args []string) int {
	cfg, found, err := config.Load()
	if err != nil || !found {
		cfg = config.New()
	}
	runnerImage := cfg.RunnerImage()
	orchestratorImage := cfg.OrchestratorImage()
	webImage := cfg.WebImage()
	dockerSocket := "/var/run/docker.sock"

	needValue := func(i *int, flagName string) (string, bool) {
		if *i+1 >= len(args) {
			fmt.Fprintln(os.Stderr, color.Red("Missing value for "+flagName))
			return "", false
		}
		*i++
		return args[*i], true
	}

	for i := 0; i < len(args); i++ {
		arg := args[i]
		switch arg {
		case "-h", "--help":
			PrintUpdateUsage()
			return 0
		case "--runner-image":
			v, ok := needValue(&i, "--runner-image")
			if !ok {
				return 2
			}
			runnerImage = v
		case "--orchestrator-image":
			v, ok := needValue(&i, "--orchestrator-image")
			if !ok {
				return 2
			}
			orchestratorImage = v
		case "--web-image":
			v, ok := needValue(&i, "--web-image")
			if !ok {
				return 2
			}
			webImage = v
		case "--docker-socket":
			v, ok := needValue(&i, "--docker-socket")
			if !ok {
				return 2
			}
			dockerSocket = v
		default:
			fmt.Fprintln(os.Stderr, color.Red("Unknown option: "+arg))
			return 2
		}
	}

	ctx := context.Background()
	docker := dockerapi.New(dockerSocket)
	if !docker.Ping(ctx) {
		fmt.Fprintln(os.Stderr, color.Red("Docker isn't reachable - is it running?"))
		return 1
	}

	anyFailed := false

	fmt.Println(color.Bold("Updating the runner image..."))
	if !pullFresh(ctx, docker, runnerImage, "runner") {
		fmt.Println(color.Yellow("Rebuilding it from scratch instead (no cache, ~10-20 minutes)..."))
		contextDir, cleanup, err := runnerctx.MaterializeToTempDir()
		if err != nil {
			fmt.Println(color.Red("  Rebuild failed: " + err.Error()))
			anyFailed = true
		} else {
			err := docker.BuildImage(ctx, contextDir, runnerImage, func(line string) {
				fmt.Print(line)
			}, true)
			cleanup()
			if err != nil {
				fmt.Println(color.Red("  Rebuild failed: " + err.Error()))
				anyFailed = true
			} else {
				fmt.Println(color.Green("Runner image \"" + runnerImage + "\" rebuilt from scratch."))
			}
		}
	}

	fmt.Println()
	fmt.Println(color.Bold("Updating the orchestrator image..."))
	if !pullFresh(ctx, docker, orchestratorImage, "orchestrator") {
		fmt.Println(color.Dim("No local-build fallback for this one - it's meant to be pre-published. Build it " +
			"from this repo checkout (`docker compose build orchestrator`) if you need it locally."))
		anyFailed = true
	}

	fmt.Println()
	fmt.Println(color.Bold("Updating the web image..."))
	if !pullFresh(ctx, docker, webImage, "web") {
		fmt.Println(color.Dim("No local-build fallback for this one either - build it from this repo checkout " +
			"(`docker compose build web`) if you need it locally."))
		anyFailed = true
	}

	fmt.Println()
	if anyFailed {
		fmt.Println(color.Yellow("Update finished with at least one image not refreshed (see above)."))
	} else {
		fmt.Println(color.Green(color.Bold("All images up to date.")))
	}
	fmt.Println("Run " + color.Cyan("ebl clean --all") + " to reclaim disk space from the images just " +
		"replaced. If `ebl start` is currently running, re-run it to pick up the refreshed " +
		"orchestrator/web images.")
	if anyFailed {
		return 1
	}
	return 0
}
