//go:build linux || darwin

package hostinfo

import (
	"bufio"
	"os"
	"strings"

	"golang.org/x/sys/unix"
)

func utsToString(b []byte) string {
	for i, v := range b {
		if v == 0 {
			return string(b[:i])
		}
	}
	return string(b)
}

// osVersion: /etc/os-release is the standard, distro-agnostic way to get a
// proper marketing name ("Ubuntu 24.04.1 LTS", "Arch Linux", ...) on Linux -
// doesn't exist on macOS, which falls back to plain uname() output below
// instead.
func osVersion() string {
	var uts unix.Utsname
	var kernelRelease, sysname string
	if unix.Uname(&uts) == nil {
		kernelRelease = utsToString(uts.Release[:])
		sysname = utsToString(uts.Sysname[:])
	}

	prettyName := readOSReleasePrettyName()

	if prettyName != "" {
		if kernelRelease == "" {
			return prettyName
		}
		return prettyName + " (kernel " + kernelRelease + ")"
	}
	if sysname != "" {
		if kernelRelease == "" {
			return sysname
		}
		return sysname + " " + kernelRelease
	}
	return "Unknown OS"
}

func readOSReleasePrettyName() string {
	f, err := os.Open("/etc/os-release")
	if err != nil {
		return ""
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		line := scanner.Text()
		if strings.HasPrefix(line, "PRETTY_NAME=") {
			name := line[len("PRETTY_NAME="):]
			if len(name) >= 2 && name[0] == '"' && name[len(name)-1] == '"' {
				name = name[1 : len(name)-1]
			}
			return name
		}
	}
	return ""
}
