// Command ebl (expo-local-builder) is the standalone CLI for
// expo-builder-local.
//
// Subcommands:
//
//	ebl create   scaffold a brand-new Expo app (npx create-expo-app, on this machine)
//	ebl setup    one-time: install/verify Docker, pull images
//	ebl config   interactive wizard: projects folder, Expo token, ports
//	ebl start    run the orchestrator + web GUI as Docker containers
//	ebl stop     stop them
//	ebl build    build a project into a signed APK/AAB (works standalone — no
//	             setup/config/start required at all)
//	ebl update   force-refresh the runner/orchestrator/web images right now,
//	             rebuilding the runner from scratch (no cache) if it can't pull
//	ebl clean    remove ebl's own stopped build containers (--all: also cache
//	             volumes and pulled images) to reclaim disk space
//	ebl completion   print a shell completion script (bash/zsh/fish/powershell)
//
// Direct port of cli/src/main.cpp.
//
// resource_windows_amd64.syso (embeds ebl.ico as the exe's own icon plus
// FixedFileInfo/StringFileInfo version metadata - the Go analog of the old
// CMake build's automatic rc.exe invocation on resources/ebl.rc) is
// generated, not hand-written - regenerate it after editing
// versioninfo.json or swapping ebl.ico:
//
//	go run github.com/josephspurrier/goversioninfo/cmd/goversioninfo@v1.7.0 -o resource_windows_amd64.syso versioninfo.json
//
//go:generate go run github.com/josephspurrier/goversioninfo/cmd/goversioninfo@v1.7.0 -o resource_windows_amd64.syso versioninfo.json
package main

import (
	"fmt"
	"os"
	"runtime"

	"github.com/41vi4p/expo-builder-local/cli/internal/color"
	"github.com/41vi4p/expo-builder-local/cli/internal/commands"
	"github.com/41vi4p/expo-builder-local/cli/internal/hostinfo"
	"github.com/41vi4p/expo-builder-local/cli/internal/updatecheck"
)

// version is set at build time via -ldflags "-X main.version=x.y.z" (the Go
// analog of CMake's EXPO_BUILDER_CLI_VERSION compile definition).
var version = "0.0.0-dev"

func platformString() string {
	switch runtime.GOOS {
	case "windows":
		return "Windows"
	case "darwin":
		return "macOS"
	case "linux":
		return "Linux"
	default:
		return "Unknown"
	}
}

func archString() string {
	switch runtime.GOARCH {
	case "arm64":
		return "arm64"
	case "amd64":
		return "x86_64"
	case "386":
		return "x86"
	default:
		return runtime.GOARCH
	}
}

const topLevelUsage = `ebl <command> [options]

expo-local-builder - build managed Expo projects into signed Android APK/AABs in a
disposable Docker container, with an optional web GUI.

Commands:
  create    Scaffold a brand-new Expo app (npx create-expo-app, on this machine)
  setup     One-time: check/install Docker, pull images
  config    Interactive wizard: projects folder, Expo token, ports
  start     Run the orchestrator + web GUI (as Docker containers)
  stop      Stop the orchestrator + web GUI
  build     Build a project - works standalone, no setup/config/start required
  update    Force-refresh the runner/orchestrator/web images right now
  clean     Remove stopped build containers (--all: also cache volumes/images)
  completion   Print a shell completion script (bash/zsh/fish/powershell)

Run ` + "`ebl <command> --help`" + ` for command-specific options. ` + "`ebl create . myapp`" + ` to
scaffold a new app, or ` + "`ebl build .`" + ` if you just want a build right now.

  -h, --help      Show this help
  -v, --version   Show version
      --about     Show project/developer/license/repository info
`

func printTopLevelUsage() { fmt.Print(topLevelUsage) }

func printVersion() {
	fmt.Printf("ebl %s - build managed Expo (SDK 56+) projects into signed Android APK/AABs, "+
		"in a disposable Docker container\n\n", version)
	fmt.Printf("Built:    Go %s\n", runtime.Version())
	fmt.Printf("Platform: %s %s\n", platformString(), archString())
	fmt.Printf("Host:     %s\n", hostinfo.OSVersion())

	// Rate-limited to at most one real network check per 24h - --version is
	// exactly the moment a user is already asking about versions, so a
	// short (<=2.5s), best-effort check here is expected rather than a
	// surprise background network call on every other command.
	if latest, ok := updatecheck.CheckForNewerVersion(version); ok {
		fmt.Println()
		fmt.Println(color.Yellow("A newer ebl is available: " + latest + " (you have " + version + ")"))
		fmt.Println(color.Dim("  https://github.com/41vi4p/expo-builder-local/releases/latest"))
	}
}

const aboutText = `ebl (expo-local-builder) v%s

expo-builder-local - scaffold, build, and manage Expo (SDK 56+) projects end to
end: ` + "`ebl create`" + ` to start a new app, ` + "`ebl build`" + ` to turn it into a signed
Android APK/AAB entirely on your own machine via a disposable Docker container,
with an optional web GUI. Not affiliated with Expo/Google.

Developer:    41vi4p
License:      GNU General Public License v3.0 (GPL-3.0)
Repository:   https://github.com/41vi4p/expo-builder-local
`

func printAbout() { fmt.Printf(aboutText, version) }

func main() {
	if len(os.Args) == 1 {
		printTopLevelUsage()
		os.Exit(0)
	}

	command := os.Args[1]
	switch command {
	case "-h", "--help":
		printTopLevelUsage()
		os.Exit(0)
	case "-v", "--version":
		printVersion()
		os.Exit(0)
	case "--about", "about":
		printAbout()
		os.Exit(0)
	}

	subArgs := os.Args[2:]

	var code int
	switch command {
	case "create":
		code = commands.RunCreate(subArgs)
	case "build":
		code = commands.RunBuild(subArgs)
	case "setup":
		code = commands.RunSetup(subArgs)
	case "config":
		code = commands.RunConfig(subArgs)
	case "start":
		code = commands.RunStart(subArgs)
	case "stop":
		code = commands.RunStop(subArgs)
	case "update":
		code = commands.RunUpdate(subArgs)
	case "clean":
		code = commands.RunClean(subArgs)
	case "completion":
		code = commands.RunCompletion(subArgs)
	default:
		fmt.Fprintf(os.Stderr, "Unknown command: %s\n\n", command)
		printTopLevelUsage()
		os.Exit(2)
	}
	os.Exit(code)
}
