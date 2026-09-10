// Package winpath converts a Windows drive-letter path into the form Docker
// Desktop's Engine API expects for bind mounts. Identity function on every
// other platform - Docker Desktop's daemon runs inside its own Linux VM, so a
// raw drive-letter path ("D:\Projects\App") means nothing to it; the real
// `docker` CLI performs this same client-side conversion. Direct port of
// cli/src/winpath.cpp.
package winpath

import "runtime"

// ToDockerBindPath converts "D:\Projects\App" to "//d/Projects/App" on
// Windows; returns hostPath unchanged on every other platform.
func ToDockerBindPath(hostPath string) string {
	if runtime.GOOS != "windows" {
		return hostPath
	}
	return toDockerBindPathWindows(hostPath)
}

// toDockerBindPathWindows does the actual translation - split out as a plain
// function (not gated by a build tag) so it can be unit-tested on any
// platform, matching how cli/tests exercises this logic without a Windows
// machine.
func toDockerBindPathWindows(hostPath string) string {
	// Only a plain drive-letter path ("C:\..." or "C:/...") needs translating -
	// a path that's already forward-slashed and driveless is passed through
	// as-is.
	if len(hostPath) < 3 || hostPath[1] != ':' || (hostPath[2] != '\\' && hostPath[2] != '/') {
		return hostPath
	}

	drive := hostPath[0]
	if drive >= 'A' && drive <= 'Z' {
		drive = drive - 'A' + 'a'
	}

	out := make([]byte, 0, len(hostPath)+1)
	out = append(out, '/', '/', drive, '/')
	for i := 3; i < len(hostPath); i++ {
		c := hostPath[i]
		if c == '\\' {
			c = '/'
		}
		out = append(out, c)
	}
	return string(out)
}
