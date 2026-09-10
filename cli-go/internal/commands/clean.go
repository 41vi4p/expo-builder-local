package commands

import (
	"context"
	"fmt"
	"os"

	"github.com/41vi4p/expo-builder-local/cli/internal/color"
	"github.com/41vi4p/expo-builder-local/cli/internal/config"
	"github.com/41vi4p/expo-builder-local/cli/internal/dockerapi"
)

const cleanUsage = `ebl clean [options]

Removes ebl's own stopped build containers (the disposable per-build containers
` + "`ebl build`" + ` creates, left behind if a build was interrupted or the client
disconnected before cleanup). With --all, also removes the shared gradle/npm
cache volumes and the runner/orchestrator/web images ` + "`ebl setup`/`ebl build`" + `
pulled - the next build/setup just re-pulls/re-creates whatever it needs, so
this is safe, just slower on the next run.

Options:
      --all                      Also remove cache volumes and pulled images,
                                  not just stopped containers
      --gradle-cache-volume <n>  Gradle cache volume name (default:
                                  expo-builder-local_gradle-cache - must match
                                  what ` + "`ebl build`" + ` used, if you customized it)
      --npm-cache-volume <n>     npm cache volume name (default:
                                  expo-builder-local_npm-cache)
      --docker-socket <path>     Docker socket path (default: /var/run/docker.sock;
                                  ignored on Windows)
  -h, --help                     Show this help
`

// PrintCleanUsage prints `ebl clean`'s help text.
func PrintCleanUsage() { fmt.Print(cleanUsage) }

// RunClean implements `ebl clean [options]`. args are the arguments after
// the "clean" subcommand token. Returns the process exit code.
func RunClean(args []string) int {
	all := false
	gradleCacheVolume := "expo-builder-local_gradle-cache"
	npmCacheVolume := "expo-builder-local_npm-cache"
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
			PrintCleanUsage()
			return 0
		case "--all":
			all = true
		case "--gradle-cache-volume":
			v, ok := needValue(&i, "--gradle-cache-volume")
			if !ok {
				return 2
			}
			gradleCacheVolume = v
		case "--npm-cache-volume":
			v, ok := needValue(&i, "--npm-cache-volume")
			if !ok {
				return 2
			}
			npmCacheVolume = v
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

	fmt.Println(color.Bold("Removing stopped build containers..."))
	containers, err := docker.ListBuildContainers(ctx)
	if err != nil {
		fmt.Fprintln(os.Stderr, color.Red(err.Error()))
		return 1
	}

	anyRunning := false
	removedContainers := 0
	for _, c := range containers {
		if c.State == "running" {
			anyRunning = true
			continue
		}
		if err := docker.RemoveContainer(ctx, c.ID); err != nil {
			shortID := c.ID
			if len(shortID) > 12 {
				shortID = shortID[:12]
			}
			fmt.Println(color.Yellow("  Could not remove " + shortID + ": " + err.Error()))
		} else {
			removedContainers++
		}
	}
	fmt.Printf("  Removed %d container(s).\n", removedContainers)

	if !all {
		fmt.Println()
		fmt.Println(color.Green("Done.") + " Run with " + color.Cyan("--all") + " to also clear cache volumes and pulled images.")
		return 0
	}

	// Refuse rather than force a live build's cache volume out from under it -
	// Docker's own DELETE /volumes already fails on an in-use volume, but
	// checking here gives a clearer message than surfacing that raw error to
	// the user.
	if anyRunning {
		fmt.Println()
		fmt.Fprintln(os.Stderr, color.Red("A build is currently running - refusing to remove cache volumes/images out from under it. "+
			"Wait for it to finish (or stop it) and re-run `ebl clean --all`."))
		return 1
	}

	fmt.Println()
	fmt.Println(color.Bold("Removing cache volumes..."))
	for _, vol := range []string{gradleCacheVolume, npmCacheVolume} {
		if err := docker.RemoveVolume(ctx, vol); err != nil {
			fmt.Println(color.Yellow("  " + err.Error()))
		} else {
			fmt.Printf("  Removed volume %q.\n", vol)
		}
	}

	cfg, found, err := config.Load()
	if err != nil || !found {
		cfg = config.New()
	}
	fmt.Println()
	fmt.Println(color.Bold("Removing pulled images..."))
	for _, tag := range []string{cfg.RunnerImage(), cfg.OrchestratorImage(), cfg.WebImage()} {
		if err := docker.RemoveImage(ctx, tag); err != nil {
			fmt.Println(color.Yellow("  " + err.Error()))
		} else {
			fmt.Printf("  Removed image %q.\n", tag)
		}
	}

	fmt.Println()
	fmt.Println(color.Green(color.Bold("Done.")) + " The next `ebl build`/`ebl setup` will re-pull/re-create whatever it needs.")
	return 0
}
