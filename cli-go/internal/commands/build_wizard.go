package commands

import (
	"fmt"
	"io"

	"github.com/41vi4p/expo-builder-local/cli/internal/color"
	"github.com/41vi4p/expo-builder-local/cli/internal/detect"
	"github.com/41vi4p/expo-builder-local/cli/internal/prompt"
	"github.com/41vi4p/expo-builder-local/cli/internal/tuimenu"
)

// runBuildWizard implements --tui's interactive setup: arrow-key menus for
// artifact/profile/engine/signing, pre-selected from whatever opts already
// has (flags, or their defaults) so `--tui --engine gradle` starts with
// Gradle highlighted rather than ignoring it. Mutates opts in place with the
// final choices; the caller then continues straight into the same
// validation/build path a flag-driven invocation would hit, completely
// unchanged. Returns false if the user cancelled at any step (Escape/
// Ctrl-C, or answering no to the final confirmation) - the caller is
// expected to just exit in that case, nothing has started yet.
func runBuildWizard(opts *buildOptions, project detect.ProjectInfo, log io.Writer) bool {
	fmt.Fprintln(log)
	fmt.Fprintln(log, color.Bold("Interactive build setup")+color.Dim(" - Up/Down to move, Enter to select, Esc to cancel"))

	defaultArtifact := opts.artifact
	if !opts.hasArtifact {
		if opts.prod {
			defaultArtifact = "aab"
		} else {
			defaultArtifact = "apk"
		}
	}
	artifactDefault := 0
	if defaultArtifact == "aab" {
		artifactDefault = 1
	}
	artifactChoice := tuimenu.Select("Artifact type",
		[]string{"APK  - installs directly on a device", "AAB  - Play Store bundle"}, artifactDefault)
	if artifactChoice < 0 {
		return false
	}
	if artifactChoice == 1 {
		opts.artifact = "aab"
	} else {
		opts.artifact = "apk"
	}
	opts.hasArtifact = true

	defaultProfile := opts.profile
	if !opts.hasProfile {
		if opts.prod {
			defaultProfile = "production"
		} else {
			defaultProfile = "preview"
		}
	}
	if len(project.EasProfiles) > 0 {
		profileDefault := 0
		for i, p := range project.EasProfiles {
			if p == defaultProfile {
				profileDefault = i
			}
		}
		profileChoice := tuimenu.Select("Build profile (from eas.json)", project.EasProfiles, profileDefault)
		if profileChoice < 0 {
			return false
		}
		opts.profile = project.EasProfiles[profileChoice]
	} else {
		entered := prompt.String("Build profile", defaultProfile)
		if entered == "" {
			opts.profile = defaultProfile
		} else {
			opts.profile = entered
		}
	}
	opts.hasProfile = true

	engineValues := []string{"auto", "gradle", "eas"}
	engineDefault := 0
	for i, v := range engineValues {
		if v == opts.engine {
			engineDefault = i
		}
	}
	engineChoice := tuimenu.Select("Build engine", []string{
		"Auto (recommended) - eas if the project's configured for it, else gradle",
		"Gradle - local, fully offline", "EAS - local, needs an Expo access token",
	}, engineDefault)
	if engineChoice < 0 {
		return false
	}
	opts.engine = engineValues[engineChoice]

	signDefault := 0
	if opts.release {
		signDefault = 1
	}
	signChoice := tuimenu.Select("Signing",
		[]string{"Debug - fast, not for the Play Store", "Release - sign with a real keystore"}, signDefault)
	if signChoice < 0 {
		return false
	}
	opts.release = signChoice == 1

	if opts.release {
		keystorePath := prompt.String("Path to .jks/.keystore file", opts.keystore)
		if keystorePath == "" {
			fmt.Fprintln(log, color.Red("A keystore path is required for release signing."))
			return false
		}
		opts.keystore, opts.hasKeystore = keystorePath, true
		storePw := prompt.Hidden("Keystore password")
		if storePw != "" {
			opts.storePassword = storePw
		}
		alias := prompt.String("Key alias", opts.keyAlias)
		if alias != "" {
			opts.keyAlias = alias
		}
		keyPw := prompt.Hidden("Key password (blank = same as store password)")
		if keyPw != "" {
			opts.keyPassword = keyPw
		}
	}

	fmt.Fprintln(log)
	fmt.Fprintln(log, color.Bold("Ready:"))
	fmt.Fprintln(log, "  "+color.Dim("Artifact:")+" "+opts.artifact)
	fmt.Fprintln(log, "  "+color.Dim("Profile: ")+" "+opts.profile)
	fmt.Fprintln(log, "  "+color.Dim("Engine:  ")+" "+opts.engine)
	signingLabel := "debug"
	if opts.release {
		signingLabel = "release"
	}
	fmt.Fprintln(log, "  "+color.Dim("Signing: ")+" "+signingLabel)
	fmt.Fprintln(log)

	proceed := prompt.String("Start the build? [Y/n]", "Y")
	return proceed != "" && (proceed[0] == 'y' || proceed[0] == 'Y')
}
