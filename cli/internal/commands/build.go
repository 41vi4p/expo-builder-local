package commands

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"github.com/41vi4p/expo-builder-local/cli/internal/color"
	"github.com/41vi4p/expo-builder-local/cli/internal/config"
	"github.com/41vi4p/expo-builder-local/cli/internal/detect"
	"github.com/41vi4p/expo-builder-local/cli/internal/dockerapi"
	"github.com/41vi4p/expo-builder-local/cli/internal/prompt"
	"github.com/41vi4p/expo-builder-local/cli/internal/pullprogress"
	"github.com/41vi4p/expo-builder-local/cli/internal/runnerctx"
)

const buildUsage = `ebl build [path] [options]

Build a managed Expo project into a signed Android APK/AAB in a disposable Docker
container. Run it from anywhere, pointing at any Expo project root.

Arguments:
  path                     Path to the Expo project root (default: .)

Options:
      --prod                     Shortcut for --artifact aab --profile production
                                  (defaults otherwise: apk / preview)
  -a, --artifact <type>          apk or aab (default: apk, or aab with --prod)
  -p, --profile <name>           eas.json build profile (default: "preview", or
                                  "production" with --prod, or the project's first
                                  declared profile)
  -e, --engine <engine>          auto, gradle, or eas (default: eas)
      --release                 Sign with a real keystore instead of the debug keystore
      --keystore <path>          Path to a .jks/.keystore file (required with --release)
      --store-password <pw>      Keystore password (or set EXPO_BUILDER_STORE_PASSWORD)
      --key-alias <alias>        Key alias (required with --release)
      --key-password <pw>        Key password (or set EXPO_BUILDER_KEY_PASSWORD;
                                  defaults to the store password)
      --expo-token <token>       Expo access token, for the eas engine. Resolved in order:
                                  this flag, EXPO_TOKEN, a .ebl-token file in the project
                                  (gitignored automatically - see below), the per-owner/
                                  default token saved by ` + "`ebl config`" + ` (auto-selected by the
                                  project's app.json "owner" field). If none of these and
                                  the engine needs one, you'll be prompted interactively,
                                  with the option to save it to .ebl-token for next time.
      --runner-image <tag>       Runner image tag (default: from ` + "`ebl config`" + ` if set,
                                  else 41vi4p/expo-builder-local-runner:latest)
      --gradle-cache-volume <n>  Docker volume for the Gradle cache
      --npm-cache-volume <n>     Docker volume for the npm cache
      --docker-socket <path>     Docker socket path (default: /var/run/docker.sock;
                                  ignored on Windows, which always talks to Docker
                                  Desktop's \\.\pipe\docker_engine)
      --json                     Print the final result as a single JSON object on
                                  stdout instead of the live dashboard - everything
                                  else (the build log, status lines) moves to
                                  stderr, so stdout carries only that one JSON line.
                                  Exit code is still 0/1/130 as normal either way.
      --logs                     Show the full raw build log instead of the live
                                  dashboard (which is the default - see below).
                                  Useful for CI logs or when you want to see every
                                  line as it happens rather than a summary view.
      --tui                      Interactive setup: pick artifact/profile/engine/
                                  signing from arrow-key menus (Up/Down, Enter,
                                  Escape to cancel) instead of passing flags, then
                                  builds normally (the live dashboard, unless you
                                  also passed --logs). Needs a real terminal on both
                                  stdin and stdout - fails outright otherwise (there's
                                  no sensible non-interactive fallback for a menu).
                                  Any --artifact/--profile/--engine/--release you did
                                  pass become that menu's pre-selected default.
  -h, --help                     Show this help

By default (no --logs/--json), ` + "`ebl build`" + ` shows a live, redrawing dashboard
instead of the raw build log: current phase + progress, elapsed time, and the
build container's CPU/memory usage. Falls back to the raw log automatically if
stdout isn't a real terminal (e.g. piped to a file or a CI log) - no need to pass
--logs yourself in that case. On failure, the last buffered log lines are still
printed afterward even in dashboard mode, so nothing is lost for debugging.
`

// PrintBuildUsage prints `ebl build`'s help text.
func PrintBuildUsage() { fmt.Print(buildUsage) }

// buildOptions mirrors cli/src/commands/build.cpp's Options struct.
type buildOptions struct {
	path              string
	prod              bool
	artifact          string
	hasArtifact       bool
	profile           string
	hasProfile        bool
	engine            string
	release           bool
	keystore          string
	hasKeystore       bool
	storePassword     string
	keyAlias          string
	keyPassword       string
	expoToken         string
	hasExpoToken      bool
	runnerImage       string
	hasRunnerImage    bool
	gradleCacheVolume string
	npmCacheVolume    string
	dockerSocket      string
	jsonOut           bool
	logs              bool
	tui               bool
}

func newBuildOptions() buildOptions {
	return buildOptions{
		path:              ".",
		engine:            "eas",
		gradleCacheVolume: "expo-builder-local_gradle-cache",
		npmCacheVolume:    "expo-builder-local_npm-cache",
		dockerSocket:      "/var/run/docker.sock",
	}
}

// parseBuildArgs returns (opts, exitCode, ok) - ok is false if parsing
// should stop and the process should exit with exitCode (0 for --help, 2
// for a usage error).
func parseBuildArgs(args []string) (buildOptions, int, bool) {
	opts := newBuildOptions()
	sawPositional := false

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
			PrintBuildUsage()
			return opts, 0, false
		case arg == "--prod":
			opts.prod = true
		case arg == "-a" || arg == "--artifact":
			v, ok := needValue(&i, "--artifact")
			if !ok {
				return opts, 2, false
			}
			opts.artifact, opts.hasArtifact = v, true
		case arg == "-p" || arg == "--profile":
			v, ok := needValue(&i, "--profile")
			if !ok {
				return opts, 2, false
			}
			opts.profile, opts.hasProfile = v, true
		case arg == "-e" || arg == "--engine":
			v, ok := needValue(&i, "--engine")
			if !ok {
				return opts, 2, false
			}
			opts.engine = v
		case arg == "--release":
			opts.release = true
		case arg == "--keystore":
			v, ok := needValue(&i, "--keystore")
			if !ok {
				return opts, 2, false
			}
			opts.keystore, opts.hasKeystore = v, true
		case arg == "--store-password":
			v, ok := needValue(&i, "--store-password")
			if !ok {
				return opts, 2, false
			}
			opts.storePassword = v
		case arg == "--key-alias":
			v, ok := needValue(&i, "--key-alias")
			if !ok {
				return opts, 2, false
			}
			opts.keyAlias = v
		case arg == "--key-password":
			v, ok := needValue(&i, "--key-password")
			if !ok {
				return opts, 2, false
			}
			opts.keyPassword = v
		case arg == "--expo-token":
			v, ok := needValue(&i, "--expo-token")
			if !ok {
				return opts, 2, false
			}
			opts.expoToken, opts.hasExpoToken = v, true
		case arg == "--runner-image":
			v, ok := needValue(&i, "--runner-image")
			if !ok {
				return opts, 2, false
			}
			opts.runnerImage, opts.hasRunnerImage = v, true
		case arg == "--gradle-cache-volume":
			v, ok := needValue(&i, "--gradle-cache-volume")
			if !ok {
				return opts, 2, false
			}
			opts.gradleCacheVolume = v
		case arg == "--npm-cache-volume":
			v, ok := needValue(&i, "--npm-cache-volume")
			if !ok {
				return opts, 2, false
			}
			opts.npmCacheVolume = v
		case arg == "--docker-socket":
			v, ok := needValue(&i, "--docker-socket")
			if !ok {
				return opts, 2, false
			}
			opts.dockerSocket = v
		case arg == "--json":
			opts.jsonOut = true
		case arg == "--logs":
			opts.logs = true
		case arg == "--status":
			// Deprecated, kept as a silent no-op: v0.24.0/v0.25.0's --status flag
			// used to be how you opted INTO the live dashboard - it's the default
			// now, so there's nothing left for this flag to actually do.
			// Accepting it (rather than "Unknown option") means anyone who
			// already has --status in a script/alias doesn't suddenly start
			// seeing an error.
		case arg == "--tui":
			opts.tui = true
		case len(arg) > 0 && arg[0] == '-':
			fmt.Fprintln(os.Stderr, color.Red("Unknown option: "+arg))
			return opts, 2, false
		case sawPositional:
			fmt.Fprintln(os.Stderr, color.Red("Unexpected extra argument: "+arg))
			return opts, 2, false
		default:
			opts.path = arg
			sawPositional = true
		}
	}
	return opts, 0, true
}

// A project-local, gitignored fallback for the Expo token - for a team where
// different developers/CI machines build the same checkout but don't share
// `ebl config`'s global ~/.config/ebl state (or don't want a token that
// broad).
const projectTokenFilename = ".ebl-token"

func readProjectTokenFile(appPath string) (string, bool) {
	data, err := os.ReadFile(filepath.Join(appPath, projectTokenFilename))
	if err != nil {
		return "", false
	}
	content := strings.TrimRight(string(data), "\n\r ")
	if content == "" {
		return "", false
	}
	return content, true
}

// ensureGitignored appends entry as its own line in <appPath>/.gitignore,
// unless already present (exact-line match) - creates the file if it
// doesn't exist yet.
func ensureGitignored(appPath, entry string) error {
	gitignorePath := filepath.Join(appPath, ".gitignore")
	existing, _ := os.ReadFile(gitignorePath)
	for _, line := range strings.Split(string(existing), "\n") {
		if strings.TrimRight(line, "\r") == entry {
			return nil
		}
	}
	f, err := os.OpenFile(gitignorePath, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0o644)
	if err != nil {
		return err
	}
	defer f.Close()
	if len(existing) > 0 && existing[len(existing)-1] != '\n' {
		f.WriteString("\n")
	}
	_, err = f.WriteString(entry + "\n")
	return err
}

func saveProjectTokenFile(appPath, token string) error {
	tokenPath := filepath.Join(appPath, projectTokenFilename)
	if err := os.WriteFile(tokenPath, []byte(token+"\n"), 0o600); err != nil {
		return fmt.Errorf("could not write %s: %w", tokenPath, err)
	}
	return ensureGitignored(appPath, projectTokenFilename)
}

// toHostArtifactPath translates the @@ARTIFACT: marker's container-rooted
// path (build-entrypoint.sh reports it relative to dockerapi.ContainerAppDir)
// back to the real host path - the CLI runs natively on the host, but the
// container reports paths rooted at its own bind-mount point.
func toHostArtifactPath(appPath, containerPath string) string {
	prefix := dockerapi.ContainerAppDir + "/"
	if !strings.HasPrefix(containerPath, prefix) {
		return containerPath
	}
	return filepath.Join(appPath, strings.TrimPrefix(containerPath, prefix))
}

// stripAnsiEscapes strips ANSI/terminal control sequences (CSI - `ESC [ ...
// final-byte`, OSC - `ESC ] ... BEL/ST`, and any other C0 control byte
// except tab) out of a line before it goes into the dashboard's own
// recent-log-lines tail or the failure-path log dump. Needed because
// build-tool spinners (npm's own among them) draw their animation with raw
// cursor-reposition/erase codes (ESC[1G/ESC[0K) rather than \r/\n between
// frames - a whole burst of spinner frames lands in onChunk as one single
// "line" packed with those codes, and printing that unmodified as part of
// our own redraw-in-place dashboard frame means those codes execute for
// real: they move the cursor and erase text *within our own just-painted
// frame*, silently wiping out whatever we'd already drawn instead of just
// looking like garbled text. Raw --logs passthrough deliberately does NOT
// go through this - spinners are supposed to animate normally there, since
// that mode is a real terminal stream, not a redraw-in-place frame with its
// own cursor bookkeeping.
func stripAnsiEscapes(s string) string {
	var out strings.Builder
	out.Grow(len(s))
	for i := 0; i < len(s); {
		c := s[i]
		if c == 0x1b && i+1 < len(s) {
			next := s[i+1]
			if next == '[' {
				j := i + 2
				for j < len(s) && !(s[j] >= 0x40 && s[j] <= 0x7e) {
					j++
				}
				if j < len(s) {
					i = j + 1
				} else {
					i = len(s)
				}
				continue
			}
			if next == ']' {
				j := i + 2
				for j < len(s) && s[j] != '\a' && !(s[j] == 0x1b && j+1 < len(s) && s[j+1] == '\\') {
					j++
				}
				if j < len(s) && s[j] == '\a' {
					i = j + 1
				} else if j+1 < len(s) {
					i = j + 2
				} else {
					i = len(s)
				}
				continue
			}
			i += 2
			continue
		}
		if c < 0x20 && c != '\t' {
			i++
			continue
		}
		out.WriteByte(c)
		i++
	}
	return out.String()
}

func formatBytes(bytes uint64) string {
	if bytes < 1024*1024 {
		return fmt.Sprintf("%.0f KB", float64(bytes)/1024.0)
	}
	return fmt.Sprintf("%.1f MB", float64(bytes)/(1024.0*1024.0))
}

func formatBuildDuration(seconds int64) string {
	m := seconds / 60
	s := seconds % 60
	if m > 0 {
		return fmt.Sprintf("%dm %ds", m, s)
	}
	return fmt.Sprintf("%ds", s)
}

// ensureRunnerImage always tries to pull first, whether or not the image
// already exists locally - Docker's pull is idempotent (only transfers
// changed layers, no-ops quickly when already current), so this doubles as
// the update check on every build. Falls back, in order: the cached local
// image if the pull fails but one is present (offline or Hub unreachable);
// building it from the bundled context if neither is available (fully
// offline-capable). All status output goes to log, not stdout directly - in
// --json mode log is stderr, so stdout stays clean for the final JSON.
func ensureRunnerImage(ctx context.Context, docker *dockerapi.Client, tag string, log io.Writer, logFile *os.File) error {
	fmt.Fprintln(log, color.Dim("Checking for updates to \""+tag+"\"..."))
	progress := pullprogress.New(logFile)
	err := docker.PullImage(ctx, tag, func(e dockerapi.PullEvent) {
		progress.OnEvent(e.ID, e.Status, e.Progress)
	})
	if err == nil {
		return nil
	}

	if exists, _ := docker.ImageExists(ctx, tag); exists {
		fmt.Fprintln(log, color.Dim("Update check failed ("+err.Error()+") - using the cached local image."))
		return nil
	}
	fmt.Fprintln(log, color.Dim("Pull failed ("+err.Error()+") - building it locally instead..."))

	fmt.Fprintln(log, color.Yellow("Building \""+tag+"\" now (one-time, ~10-20 minutes)..."))
	contextDir, cleanup, err := runnerctx.MaterializeToTempDir()
	if err != nil {
		return err
	}
	defer cleanup()
	if err := docker.BuildImage(ctx, contextDir, tag, func(line string) {
		fmt.Fprint(log, line)
	}, false); err != nil {
		return err
	}
	fmt.Fprintln(log, color.Green("Runner image \""+tag+"\" built."))
	return nil
}

// --- JSON result shapes - one struct per exact shape the C++ CLI emits, not
// a single omitempty-everything struct, so a --json consumer sees exactly
// the same key set the C++ version always produced. ---

type earlyFailJSON struct {
	Success bool   `json:"success"`
	Error   string `json:"error"`
}

type cancelledJSON struct {
	Success         bool  `json:"success"`
	Cancelled       bool  `json:"cancelled"`
	DurationSeconds int64 `json:"durationSeconds"`
}

type buildFailJSON struct {
	Success         bool   `json:"success"`
	Error           string `json:"error"`
	DurationSeconds int64  `json:"durationSeconds"`
}

type buildSuccessJSON struct {
	Success         bool    `json:"success"`
	ArtifactPath    string  `json:"artifactPath"`
	SizeBytes       float64 `json:"sizeBytes"`
	VersionName     string  `json:"versionName"`
	VersionCode     string  `json:"versionCode"`
	ApplicationID   string  `json:"applicationId"`
	Engine          string  `json:"engine"`
	BuildNumber     string  `json:"buildNumber"`
	DurationSeconds int64   `json:"durationSeconds"`
	GitCommit       string  `json:"gitCommit"`
	GitBranch       string  `json:"gitBranch"`
	SHA256          string  `json:"sha256"`
}

func printJSON(v any) {
	data, _ := json.Marshal(v)
	fmt.Println(string(data))
}

// RunBuild implements `ebl build [path] [options]`. args are the arguments
// after the "build" subcommand token. Returns the process exit code.
func RunBuild(args []string) int {
	opts, exitCode, ok := parseBuildArgs(args)
	if !ok {
		return exitCode
	}

	// In --json mode, stdout is reserved exclusively for the one final JSON
	// line - every status/log line that would otherwise go to stdout goes to
	// log (stderr) instead, still visible for debugging, just not mixed into
	// the machine-readable output. log is stdout, unchanged, when --json
	// wasn't passed.
	logFile := os.Stdout
	if opts.jsonOut {
		logFile = os.Stderr
	}
	log := io.Writer(logFile)

	// Only fills in "error" - "success" is always false here, since every use
	// of this helper is on a path that's bailing out before a build even
	// started. Argument-parsing errors above (parseBuildArgs itself) are
	// deliberately NOT run through this - a malformed invocation isn't a case
	// where the caller can trust --json was even parsed correctly yet, so
	// those stay plain stderr text like any other CLI tool.
	fail := func(message string, code int) int {
		if opts.jsonOut {
			printJSON(earlyFailJSON{Success: false, Error: message})
		} else {
			fmt.Fprintln(os.Stderr, color.Red(message))
		}
		return code
	}

	if opts.tui && opts.jsonOut {
		return fail("--tui and --json can't be used together - both need exclusive control of stdout.", 2)
	}
	if opts.tui && (!color.Enabled() || !color.StdinIsTTY()) {
		// Unlike the dashboard, there's no sensible fallback for a menu-driven
		// wizard with nowhere to read a keypress from (or nowhere to draw it) -
		// hard error instead of silently degrading to something the user
		// didn't ask for.
		return fail("--tui needs a real terminal on both stdin and stdout - neither is piped/redirected.", 2)
	}

	// The live dashboard is the default view - --logs and --json both opt out
	// of it (in opposite directions: --logs wants the full raw text log,
	// --json wants clean machine-readable stdout, which already gets the raw
	// log on stderr regardless, making --logs redundant rather than
	// conflicting when both are passed). Falls back to the raw log
	// automatically - silently, no warning - when stdout isn't a real
	// terminal (piped/redirected): unlike the old explicit-opt-in --status
	// flag, this is now the default, so a build running in CI or redirected
	// to a file not getting a redrawing dashboard is the ordinary, expected
	// outcome, not a feature quietly failing. Same silent-degrade convention
	// color's own Enabled() already uses for ANSI colors.
	wantDashboard := !opts.logs && !opts.jsonOut && color.Enabled()

	appPath, err := filepath.Abs(opts.path)
	if err != nil {
		return fail(err.Error(), 2)
	}
	if info, err := os.Stat(appPath); err != nil || !info.IsDir() {
		return fail("Not a directory: "+appPath, 2)
	}

	project := detect.ExpoProject(appPath)
	if !project.IsExpoProject {
		return fail(appPath+" doesn't look like an Expo project: "+project.Reason, 2)
	}

	if opts.tui {
		if !runBuildWizard(&opts, project, log) {
			fmt.Fprintln(log)
			fmt.Fprintln(log, color.Dim("Cancelled - nothing was built."))
			return 130 // 128 + SIGINT, same convention the Ctrl-C build-cancellation path already uses
		}
		// No forcing needed here anymore - the dashboard is already the
		// default outcome unless the user explicitly passed --logs, same as
		// any other build.
	}

	// --prod is sugar for the production defaults, but explicit
	// --artifact/--profile (if the user passed them too) always win - or, if
	// --tui ran, this is simply whatever the wizard already resolved
	// opts.artifact to.
	artifact := opts.artifact
	if !opts.hasArtifact {
		if opts.prod {
			artifact = "aab"
		} else {
			artifact = "apk"
		}
	}
	if artifact != "apk" && artifact != "aab" {
		return fail(fmt.Sprintf("--artifact must be \"apk\" or \"aab\", got %q", artifact), 2)
	}
	if opts.engine != "auto" && opts.engine != "gradle" && opts.engine != "eas" {
		return fail(fmt.Sprintf("--engine must be \"auto\", \"gradle\", or \"eas\", got %q", opts.engine), 2)
	}
	if opts.release && !opts.hasKeystore {
		return fail("--release requires --keystore <path>", 2)
	}
	if opts.hasKeystore {
		if _, err := os.Stat(opts.keystore); err != nil {
			abs, _ := filepath.Abs(opts.keystore)
			return fail("Keystore not found: "+abs, 2)
		}
	}

	var profile string
	switch {
	case opts.hasProfile:
		profile = opts.profile
	case opts.prod:
		profile = "production"
	default:
		hasPreview := false
		for _, p := range project.EasProfiles {
			if p == "preview" {
				hasPreview = true
			}
		}
		if hasPreview || len(project.EasProfiles) == 0 {
			profile = "preview"
		} else {
			profile = project.EasProfiles[0]
		}
	}

	// `ebl config` may have saved a default/per-owner Expo token - use it as
	// a default, but any explicit flag/env var still wins. Falling back to a
	// fresh config.New() (not a hardcoded literal) when no config was ever
	// saved keeps this in sync with the real default runner image defined in
	// the config package.
	savedConfig, savedConfigFound, _ := config.Load()
	runnerImage := opts.runnerImage
	if !opts.hasRunnerImage {
		if savedConfigFound {
			runnerImage = savedConfig.RunnerImage()
		} else {
			runnerImage = config.New().RunnerImage()
		}
	}

	params := dockerapi.BuildParams{
		AppPath:      appPath,
		ArtifactType: artifact,
		Profile:      profile,
		Engine:       opts.engine,
		SigningMode:  "debug",
	}
	if opts.release {
		params.SigningMode = "release"
	}
	switch {
	case opts.hasExpoToken:
		params.ExpoToken = opts.expoToken
	case os.Getenv("EXPO_TOKEN") != "":
		params.ExpoToken = os.Getenv("EXPO_TOKEN")
	default:
		if fileToken, found := readProjectTokenFile(appPath); found {
			params.ExpoToken = fileToken
			fmt.Fprintln(log, color.Dim("Using Expo token from "+projectTokenFilename+"."))
		} else if savedConfigFound {
			params.ExpoToken = savedConfig.ExpoTokenFor(project.Owner)
			viaOwner := project.Owner != ""
			if viaOwner {
				found := false
				for _, e := range savedConfig.ExpoTokensByOwner {
					if e.Owner == project.Owner {
						found = true
						break
					}
				}
				viaOwner = found
			}
			if viaOwner {
				fmt.Fprintln(log, color.Dim("Using saved Expo token for owner \""+project.Owner+"\"."))
			}
		}
	}

	// Only the "eas" engine actually needs a token; "auto" needs one exactly
	// when it would resolve to eas (same eas.json check as
	// build-entrypoint.sh's own auto resolution), and "gradle" never does.
	// Prompting here (rather than just letting the container fail later with
	// "ENGINE=eas requires an EXPO_TOKEN") means a missing token doesn't cost
	// you the time spent pulling the runner image and starting the container
	// first.
	_, easJSONErr := os.Stat(filepath.Join(appPath, "eas.json"))
	engineNeedsToken := opts.engine == "eas" || (opts.engine == "auto" && easJSONErr == nil)
	if params.ExpoToken == "" && engineNeedsToken {
		// --json never prompts, even on a real terminal - prompt's own
		// question text always goes to stdout regardless of log, which would
		// land right in the middle of what's supposed to be a single clean
		// JSON line on stdout. Fail fast with an actionable message instead
		// of a prompt no script can answer.
		if opts.jsonOut {
			return fail(fmt.Sprintf("No Expo token found, but the %q engine needs one - pass --expo-token or set EXPO_TOKEN (--json never prompts).", opts.engine), 2)
		}
		fmt.Fprintln(log, color.Yellow(fmt.Sprintf("No Expo token found, but the %q engine needs one.", opts.engine)))
		entered := prompt.Hidden("Expo access token (from https://expo.dev/accounts/[account]/settings/access-tokens)")
		if entered == "" {
			fmt.Fprintln(os.Stderr, color.Red("No token entered - aborting."))
			return 2
		}
		params.ExpoToken = entered

		save := prompt.String("Save this token to "+projectTokenFilename+" in this project for future builds? [Y/n]", "Y")
		if save != "" && (save[0] == 'y' || save[0] == 'Y') {
			if err := saveProjectTokenFile(appPath, entered); err != nil {
				fmt.Fprintln(os.Stderr, color.Red("Could not save token file: "+err.Error()))
			} else {
				fmt.Fprintln(log, color.Green("Saved to "+filepath.Join(appPath, projectTokenFilename)+" (added to .gitignore)."))
			}
		}
	}
	if opts.release && opts.hasKeystore {
		params.HasKeystore = true
		absKeystore, _ := filepath.Abs(opts.keystore)
		params.Keystore.HostPath = absKeystore
		params.Keystore.Filename = filepath.Base(opts.keystore)
		storePassword := opts.storePassword
		if storePassword == "" {
			storePassword = os.Getenv("EXPO_BUILDER_STORE_PASSWORD")
		}
		params.Keystore.StorePassword = storePassword
		params.Keystore.KeyAlias = opts.keyAlias
		keyPassword := opts.keyPassword
		if keyPassword == "" {
			keyPassword = os.Getenv("EXPO_BUILDER_KEY_PASSWORD")
		}
		params.Keystore.KeyPassword = keyPassword
	}

	return runBuildContainer(opts, params, appPath, profile, artifact, runnerImage, project, wantDashboard, log, logFile)
}
