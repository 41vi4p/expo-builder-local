package commands

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/41vi4p/expo-builder-local/cli/internal/color"
	"github.com/41vi4p/expo-builder-local/cli/internal/config"
	"github.com/41vi4p/expo-builder-local/cli/internal/prompt"
)

func maskedPreview(secret string) string {
	if secret == "" {
		return "(not set)"
	}
	if len(secret) <= 4 {
		return "****"
	}
	return strings.Repeat("*", len(secret)-4) + secret[len(secret)-4:]
}

// PrintConfigUsage prints `ebl config`'s help text.
func PrintConfigUsage() {
	path, err := config.FilePath()
	if err != nil {
		path = "~/.config/ebl/config.json"
	}
	fmt.Printf("ebl config\n\n"+
		"Interactive wizard that saves your projects folder, Expo token, and port "+
		"settings to\n%s (secrets encrypted at rest - see README). Re-run any time\n"+
		"to change a setting; existing values are shown as defaults.\n\n"+
		"Options:\n"+
		"  -h, --help   Show this help\n", path)
}

// RunConfig implements `ebl config [options]`. args are the arguments after
// the "config" subcommand token. Returns the process exit code.
func RunConfig(args []string) int {
	for _, arg := range args {
		if arg == "-h" || arg == "--help" {
			PrintConfigUsage()
			return 0
		}
	}

	cfg, found, err := config.Load()
	if err != nil || !found {
		cfg = config.New()
	}

	fmt.Println(color.Bold("ebl config") + " - press Enter to keep the value shown in [brackets].")
	fmt.Println()

	for {
		root := prompt.String("Projects folder (parent directory of the Expo apps you'll build/browse)", cfg.ProjectsRoot)
		resolved, err := filepath.Abs(root)
		if err == nil {
			resolved = filepath.Clean(resolved)
		}
		info, statErr := os.Stat(resolved)
		if err != nil || statErr != nil || !info.IsDir() {
			fmt.Println(color.Red("Not a directory: "+resolved) + " - try again.")
			continue
		}
		cfg.ProjectsRoot = resolved
		break
	}

	fmt.Println()
	fmt.Println(color.Dim("Default Expo access token - only needed for the \"eas\" build engine, used when a " +
		"project's app.json has no owner-specific token below (or no \"owner\" field at " +
		"all). Create one at https://expo.dev/accounts/[account]/settings/access-tokens " +
		"(leave blank to skip)."))
	fmt.Println(color.Dim("Current: " + maskedPreview(cfg.ExpoToken)))
	newToken := prompt.Hidden("Default Expo access token (leave blank to keep current, type \"clear\" to remove it)")
	if newToken == "clear" {
		cfg.ExpoToken = ""
	} else if newToken != "" {
		cfg.ExpoToken = newToken
	}

	fmt.Println()
	fmt.Println(color.Dim("Per-account Expo tokens - if you build apps under more than one EAS account " +
		"(e.g. a personal account and an organization), save one token per account here. " +
		"`ebl build` auto-selects by matching the project's app.json \"owner\" field, no " +
		"need to pass --expo-token by hand."))
	if len(cfg.ExpoTokensByOwner) == 0 {
		fmt.Println(color.Dim("Current: (none saved)"))
	} else {
		fmt.Println(color.Dim("Current:"))
		for _, entry := range cfg.ExpoTokensByOwner {
			fmt.Println(color.Dim("  " + entry.Owner + ": " + maskedPreview(entry.Token)))
		}
	}
	for {
		owner := prompt.String("Account/owner to add or update (blank to finish, \"remove <owner>\" to delete one)", "")
		if owner == "" {
			break
		}
		if strings.HasPrefix(owner, "remove ") {
			target := owner[len("remove "):]
			idx := -1
			for i, e := range cfg.ExpoTokensByOwner {
				if e.Owner == target {
					idx = i
					break
				}
			}
			if idx == -1 {
				fmt.Println(color.Red("No saved token for owner \"" + target + "\"."))
			} else {
				cfg.ExpoTokensByOwner = append(cfg.ExpoTokensByOwner[:idx], cfg.ExpoTokensByOwner[idx+1:]...)
				fmt.Println(color.Green("Removed \"" + target + "\"."))
			}
			continue
		}
		token := prompt.Hidden("Expo access token for \"" + owner + "\"")
		if token == "" {
			fmt.Println(color.Red("Empty token, not saved."))
			continue
		}
		idx := -1
		for i, e := range cfg.ExpoTokensByOwner {
			if e.Owner == owner {
				idx = i
				break
			}
		}
		if idx == -1 {
			cfg.ExpoTokensByOwner = append(cfg.ExpoTokensByOwner, config.ExpoTokenEntry{Owner: owner, Token: token})
		} else {
			cfg.ExpoTokensByOwner[idx].Token = token
		}
		fmt.Println(color.Green("Saved token for \"" + owner + "\"."))
	}

	fmt.Println()
	cfg.OrchestratorPort = prompt.Int("Orchestrator port", cfg.OrchestratorPort)
	cfg.WebPort = prompt.Int("Web GUI port", cfg.WebPort)

	if err := config.Save(&cfg); err != nil {
		fmt.Fprintln(os.Stderr, color.Red("Could not save config: "+err.Error()))
		return 1
	}

	path, _ := config.FilePath()
	fmt.Println()
	fmt.Println(color.Green(color.Bold("Saved to " + path)))
	fmt.Println("  Projects folder:      " + cfg.ProjectsRoot)
	fmt.Println("  Default Expo token:   " + maskedPreview(cfg.ExpoToken))
	tokenSummary := "(none)"
	if n := len(cfg.ExpoTokensByOwner); n > 0 {
		tokenSummary = fmt.Sprintf("%d saved", n)
	}
	fmt.Println("  Per-account tokens:   " + tokenSummary)
	fmt.Printf("  Orchestrator port:    %d\n", cfg.OrchestratorPort)
	fmt.Printf("  Web GUI port:         %d\n\n", cfg.WebPort)
	fmt.Println("Next: " + color.Cyan("ebl start"))
	return 0
}
