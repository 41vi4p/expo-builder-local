// Package detect reimplements orchestrator/src/build/detect.ts's Expo-project
// detection rule so the CLI has no runtime dependency on the orchestrator/GUI
// (or Node) being installed. Direct port of cli/src/detect.cpp.
package detect

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
)

// ProjectInfo describes what was found (or why detection failed).
type ProjectInfo struct {
	IsExpoProject bool
	Name          string
	Version       string
	EasProfiles   []string
	Owner         string // app.json's expo.owner (EAS account slug) - empty if unset
	Reason        string // populated when IsExpoProject is false
}

type packageJSON struct {
	Name            string            `json:"name"`
	Version         string            `json:"version"`
	Dependencies    map[string]string `json:"dependencies"`
	DevDependencies map[string]string `json:"devDependencies"`
}

type appJSON struct {
	Expo struct {
		Owner string `json:"owner"`
	} `json:"expo"`
}

// easBuildProfileNames extracts eas.json's "build" object's key names in file
// order (json.Unmarshal into a map would randomize order - eas.json's profile
// order matters later for the --tui wizard's menu, matching the C++ version's
// insertion-ordered Json object).
func easBuildProfileNames(data []byte) ([]string, error) {
	dec := json.NewDecoder(bytes.NewReader(data))
	var root map[string]json.RawMessage
	if err := dec.Decode(&root); err != nil {
		return nil, err
	}
	buildRaw, ok := root["build"]
	if !ok {
		return nil, nil
	}
	buildDec := json.NewDecoder(bytes.NewReader(buildRaw))
	tok, err := buildDec.Token()
	if err != nil {
		return nil, err
	}
	if delim, ok := tok.(json.Delim); !ok || delim != '{' {
		return nil, nil // "build" isn't an object - no profiles
	}
	var names []string
	for buildDec.More() {
		keyTok, err := buildDec.Token()
		if err != nil {
			return nil, err
		}
		key, _ := keyTok.(string)
		names = append(names, key)
		var discard json.RawMessage
		if err := buildDec.Decode(&discard); err != nil {
			return nil, err
		}
	}
	return names, nil
}

// ExpoProject inspects dirPath for a valid managed Expo project, the same rule
// orchestrator/src/build/detect.ts uses: a package.json with an "expo"
// dependency (dependencies or devDependencies).
func ExpoProject(dirPath string) ProjectInfo {
	var info ProjectInfo

	pkgPath := filepath.Join(dirPath, "package.json")
	pkgText, err := os.ReadFile(pkgPath)
	if err != nil {
		info.Reason = fmt.Sprintf("No package.json found in %s", dirPath)
		return info
	}

	var pkg packageJSON
	if err := json.Unmarshal(pkgText, &pkg); err != nil {
		info.Reason = fmt.Sprintf("package.json exists but could not be parsed: %v", err)
		return info
	}

	_, hasDep := pkg.Dependencies["expo"]
	_, hasDevDep := pkg.DevDependencies["expo"]
	if !hasDep && !hasDevDep {
		info.Reason = "package.json has no 'expo' dependency"
		return info
	}

	info.IsExpoProject = true
	info.Name = pkg.Name
	info.Version = pkg.Version

	if appText, err := os.ReadFile(filepath.Join(dirPath, "app.json")); err == nil {
		var app appJSON
		if json.Unmarshal(appText, &app) == nil {
			info.Owner = app.Expo.Owner
		}
		// malformed app.json just means no owner detected - not fatal for detection
	}

	if easText, err := os.ReadFile(filepath.Join(dirPath, "eas.json")); err == nil {
		if names, err := easBuildProfileNames(easText); err == nil {
			info.EasProfiles = names
		}
		// malformed eas.json just means no profile list - not fatal for detection
	}

	return info
}
