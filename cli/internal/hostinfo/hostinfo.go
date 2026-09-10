// Package hostinfo provides a best-effort human-readable OS/version string
// for `ebl --version` (e.g. "Windows 11 Pro, 23H2 (Build 22631.3737)" vs.
// "Ubuntu 24.04.1 LTS (kernel 6.8.0-...)"). Direct port of
// cli/src/host_info.hpp/.cpp.
package hostinfo

// OSVersion returns a best-effort human-readable OS/version string.
func OSVersion() string {
	return osVersion()
}
