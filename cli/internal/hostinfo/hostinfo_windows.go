//go:build windows

package hostinfo

import (
	"strconv"

	"golang.org/x/sys/windows/registry"
)

// osVersion: GetVersionEx()/VerifyVersionInfo() are documented as lying to
// any process without an explicit Windows 10/11-aware manifest entry
// (returning a fixed "Windows 8" identity instead) - reading these registry
// values directly is the standard workaround, and is also just plain richer
// info than either API ever exposed (build number, update revision, the
// actual marketing name).
func osVersion() string {
	key, err := registry.OpenKey(registry.LOCAL_MACHINE, `SOFTWARE\Microsoft\Windows NT\CurrentVersion`, registry.QUERY_VALUE)
	if err != nil {
		return "Windows (version unknown)"
	}
	defer key.Close()

	productName, _, _ := key.GetStringValue("ProductName")
	if productName == "" {
		return "Windows (version unknown)"
	}

	displayVersion, _, _ := key.GetStringValue("DisplayVersion")
	if displayVersion == "" {
		displayVersion, _, _ = key.GetStringValue("ReleaseId")
	}
	buildNumber, _, _ := key.GetStringValue("CurrentBuildNumber")
	ubr, _, _ := key.GetIntegerValue("UBR")

	result := productName
	if displayVersion != "" {
		result += ", " + displayVersion
	}
	if buildNumber != "" {
		result += " (Build " + buildNumber
		if ubr > 0 {
			result += "." + strconv.FormatUint(ubr, 10)
		}
		result += ")"
	}
	return result
}
