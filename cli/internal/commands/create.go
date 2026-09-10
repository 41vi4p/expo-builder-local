package commands

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/41vi4p/expo-builder-local/cli/internal/color"
	"github.com/41vi4p/expo-builder-local/cli/internal/hostprocess"
)

const createUsage = `ebl create <path> <name> [options]

Scaffolds a brand-new Expo app named <name> inside directory <path> (e.g.
` + "`ebl create . myapp`" + ` creates ./myapp), via ` + "`npx create-expo-app`" + ` - runs directly
on this machine, not inside a Docker container (scaffolding needs nothing that a
disposable build container provides; only ` + "`ebl build`" + ` actually needs one).

Arguments:
  path                     Directory the new app's folder will be created inside
  name                     Name of the new app (and its folder)

Options:
      --template <name>          Passed straight through to create-expo-app's own
                                  --template flag (a template name or npm package)
  -h, --help                     Show this help

Requires Node.js (for ` + "`npx`" + `) on PATH - if you don't have it, install it from
https://nodejs.org first.
`

// PrintCreateUsage prints `ebl create`'s help text.
func PrintCreateUsage() { fmt.Print(createUsage) }

// RunCreate implements `ebl create <path> <name> [options]`. args are the
// arguments after the "create" subcommand token. Returns the process exit
// code.
func RunCreate(args []string) int {
	var path, name string
	var templateName string
	haveTemplate := false
	sawPath, sawName := false, false

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
		switch {
		case arg == "-h" || arg == "--help":
			PrintCreateUsage()
			return 0
		case arg == "--template":
			v, ok := needValue(&i, "--template")
			if !ok {
				return 2
			}
			templateName, haveTemplate = v, true
		case len(arg) > 0 && arg[0] == '-':
			fmt.Fprintln(os.Stderr, color.Red("Unknown option: "+arg))
			return 2
		case !sawPath:
			path, sawPath = arg, true
		case !sawName:
			name, sawName = arg, true
		default:
			fmt.Fprintln(os.Stderr, color.Red("Unexpected extra argument: "+arg))
			return 2
		}
	}

	if !sawPath || !sawName {
		fmt.Fprintln(os.Stderr, color.Red("Usage: ebl create <path> <name> [options]"))
		return 2
	}

	targetDir, err := filepath.Abs(path)
	if err != nil {
		fmt.Fprintln(os.Stderr, color.Red(err.Error()))
		return 2
	}
	targetDir = filepath.Clean(targetDir)
	if info, err := os.Stat(targetDir); err != nil || !info.IsDir() {
		fmt.Fprintln(os.Stderr, color.Red("Not a directory: "+targetDir))
		return 2
	}

	appDir := filepath.Join(targetDir, name)
	if _, err := os.Stat(appDir); err == nil {
		fmt.Fprintln(os.Stderr, color.Red(appDir+" already exists."))
		return 2
	}

	npxArgs := []string{"npx", "create-expo-app@latest", name}
	if haveTemplate {
		npxArgs = append(npxArgs, "--template", templateName)
	}

	fmt.Println(color.Bold("Creating " + color.Cyan(appDir)))
	fmt.Println()

	exitCode, err := hostprocess.RunStreaming(npxArgs, targetDir, func(chunk []byte) {
		os.Stdout.Write(chunk)
	})
	if err != nil {
		fmt.Fprintln(os.Stderr, color.Red(err.Error()))
		return 1
	}

	if exitCode == hostprocess.NotFoundExitCode {
		fmt.Println()
		fmt.Fprintln(os.Stderr, color.Red("`npx` not found - install Node.js from https://nodejs.org, then try again."))
		return 1
	}
	if exitCode != 0 {
		fmt.Println()
		fmt.Fprintln(os.Stderr, color.Red(fmt.Sprintf("create-expo-app exited with status %d", exitCode)))
		return 1
	}

	fmt.Println()
	fmt.Println(color.Green(color.Bold("Created " + appDir)))
	fmt.Println(color.Dim("Next:"))
	fmt.Println("  cd " + name)
	fmt.Println("  ebl setup        " + color.Dim("(first time only)"))
	fmt.Println("  ebl build .")
	return 0
}
