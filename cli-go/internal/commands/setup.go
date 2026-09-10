package commands

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"runtime"
	"time"

	"github.com/41vi4p/expo-builder-local/cli/internal/color"
	"github.com/41vi4p/expo-builder-local/cli/internal/config"
	"github.com/41vi4p/expo-builder-local/cli/internal/dockerapi"
	"github.com/41vi4p/expo-builder-local/cli/internal/prompt"
	"github.com/41vi4p/expo-builder-local/cli/internal/pullprogress"
)

const setupUsageWindows = `ebl setup

One-time setup: makes sure Docker Desktop is reachable, then pulls the runner/
orchestrator/web images so ` + "`ebl build`/`ebl start`" + ` are ready to go immediately.

Options:
  -h, --help   Show this help
`

const setupUsagePOSIX = `ebl setup

One-time setup: makes sure Docker is installed and running, offers to install it if
not (official convenience script, requires sudo - also enables+starts the systemd
service and adds you to the docker group, which the script alone doesn't do), then
pulls the runner/orchestrator/web images so ` + "`ebl build`/`ebl start`" + ` are ready to go
immediately.

Options:
  -h, --help   Show this help
`

// PrintSetupUsage prints `ebl setup`'s help text.
func PrintSetupUsage() {
	if runtime.GOOS == "windows" {
		fmt.Print(setupUsageWindows)
	} else {
		fmt.Print(setupUsagePOSIX)
	}
}

// runShell runs a shell command line (needed for things like the piped
// `curl ... | sh` convenience-script invocation) and reports whether it
// exited zero. Equivalent to cli/src/commands/setup.cpp's use of
// std::system().
func runShell(cmdLine string) bool {
	cmd := exec.Command("sh", "-c", cmdLine)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Stdin = os.Stdin
	return cmd.Run() == nil
}

// runShellQuiet is runShell with output discarded - for the enable/start
// systemctl calls whose success is checked separately (via a follow-up
// docker.Ping), not by inspecting their own output.
func runShellQuiet(cmdLine string) {
	cmd := exec.Command("sh", "-c", cmdLine)
	cmd.Run()
}

func ensureDockerReachablePOSIX(ctx context.Context, docker *dockerapi.Client) int {
	if _, err := exec.LookPath("docker"); err != nil {
		fmt.Println("Docker doesn't appear to be installed.")
		if !prompt.YesNo("Install it now via the official convenience script (curl -fsSL " +
			"https://get.docker.com | sh)? This will ask for sudo.") {
			fmt.Println("Skipped. Install Docker yourself (https://docs.docker.com/engine/install/) " +
				"and re-run `ebl setup`.")
			return 1
		}
		fmt.Println(color.Dim("Running the Docker install script..."))
		if !runShell("curl -fsSL https://get.docker.com | sh") {
			fmt.Fprintln(os.Stderr, color.Red("Docker install script failed - install it manually and re-run `ebl setup`."))
			return 1
		}
		fmt.Println(color.Green("Docker installed."))

		// get.docker.com installs the packages but, unlike the manual apt
		// flow, doesn't enable/start the systemd service or add the invoking
		// user to the docker group - do both explicitly so a fresh install
		// is actually usable, not just present.
		runShellQuiet("sudo systemctl enable docker >/dev/null 2>&1")
		runShellQuiet("sudo systemctl start docker >/dev/null 2>&1")
		if sudoUser := os.Getenv("USER"); sudoUser != "" {
			if runShell("sudo usermod -aG docker '" + sudoUser + "'") {
				fmt.Println(color.Dim("Added \"" + sudoUser + "\" to the docker group."))
			}
		}

		if !docker.Ping(ctx) {
			fmt.Println(color.Yellow("Docker is installed but not reachable from this shell yet - you likely need to log " +
				"out and back in (or run `newgrp docker`) so your user picks up docker-group " +
				"membership, then re-run `ebl setup`."))
			return 1
		}
	} else {
		fmt.Println("Docker is installed but the daemon isn't reachable - trying to start it " +
			"(sudo systemctl start docker)...")
		runShellQuiet("sudo systemctl enable docker >/dev/null 2>&1")
		runShellQuiet("sudo systemctl start docker >/dev/null 2>&1")
		if !docker.Ping(ctx) {
			fmt.Fprintln(os.Stderr, color.Red("Still not reachable after trying to start it. Check its status yourself: "+
				"sudo systemctl status docker - then re-run `ebl setup`."))
			return 1
		}
		fmt.Println(color.Green("Docker started."))
	}
	return 0
}

// RunSetup implements `ebl setup [options]`. args are the arguments after
// the "setup" subcommand token. Returns the process exit code.
func RunSetup(args []string) int {
	for _, arg := range args {
		if arg == "-h" || arg == "--help" {
			PrintSetupUsage()
			return 0
		}
	}

	ctx := context.Background()
	docker := dockerapi.New("/var/run/docker.sock")

	fmt.Println(color.Bold("Checking Docker..."))
	if !docker.Ping(ctx) {
		if runtime.GOOS == "windows" {
			// No convenience-script auto-install path here - Docker Desktop
			// is a GUI installer with its own license/reboot considerations,
			// same stance install.ps1 already takes before it even gets
			// this far.
			fmt.Fprintln(os.Stderr, color.Red("Docker Desktop isn't reachable.")+
				" Install/start it from https://www.docker.com/products/docker-desktop/ , then re-run `ebl setup`.")
			return 1
		}
		if code := ensureDockerReachablePOSIX(ctx, docker); code != 0 {
			return code
		}
	}
	fmt.Println(color.Green("Docker is up."))
	fmt.Println()

	cfg, found, err := config.Load()
	if err != nil || !found {
		cfg = config.New()
	}

	fmt.Println(color.Bold("Pulling images..."))
	anyFailed := false
	for _, tag := range []string{cfg.RunnerImage(), cfg.OrchestratorImage(), cfg.WebImage()} {
		fmt.Println(color.Dim("Pulling " + tag + "..."))
		progress := pullprogress.New(os.Stdout)
		err := docker.PullImage(ctx, tag, func(e dockerapi.PullEvent) {
			progress.OnEvent(e.ID, e.Status, e.Progress)
		})
		if err != nil {
			fmt.Println(color.Yellow("Could not pull " + tag + ": " + err.Error()))
			fmt.Println(color.Dim("(Not published yet? `ebl build`/`ebl start` will build the runner image locally " +
				"the first time it's needed; the orchestrator/web images must exist somewhere for " +
				"`ebl start` to work.)"))
			anyFailed = true
		}
	}

	cfg.SetupCompletedAt = time.Now().Unix()
	if err := config.Save(&cfg); err != nil {
		fmt.Fprintln(os.Stderr, color.Red("Could not save config: "+err.Error()))
		return 1
	}

	fmt.Println()
	if anyFailed {
		fmt.Println(color.Yellow("Setup finished with some images not pulled (see above)."))
	} else {
		fmt.Println(color.Green(color.Bold("Setup complete.")))
	}
	fmt.Println("Next: " + color.Cyan("ebl config") + " to set your projects folder and Expo token, then " +
		color.Cyan("ebl start") + ".")
	if anyFailed {
		return 1
	}
	return 0
}
