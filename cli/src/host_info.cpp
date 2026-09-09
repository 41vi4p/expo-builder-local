#include "host_info.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/utsname.h>

#include <fstream>
#endif

namespace ebl {

#ifdef _WIN32

namespace {

std::string readRegString(HKEY root, const char* subKey, const char* value) {
  char buf[256] = {0};
  DWORD size = sizeof(buf);
  if (::RegGetValueA(root, subKey, value, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return "";
  return std::string(buf);
}

DWORD readRegDword(HKEY root, const char* subKey, const char* value) {
  DWORD data = 0;
  DWORD size = sizeof(data);
  if (::RegGetValueA(root, subKey, value, RRF_RT_REG_DWORD, nullptr, &data, &size) != ERROR_SUCCESS) return 0;
  return data;
}

}  // namespace

std::string hostOsVersion() {
  // GetVersionEx()/VerifyVersionInfo() are documented as lying to any process
  // without an explicit Windows 10/11-aware manifest entry (returning a fixed
  // "Windows 8" identity instead) - reading these registry values directly is
  // the standard workaround, and is also just plain richer info than either API
  // ever exposed (build number, update revision, the actual marketing name).
  const char* key = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
  std::string productName = readRegString(HKEY_LOCAL_MACHINE, key, "ProductName");
  std::string displayVersion = readRegString(HKEY_LOCAL_MACHINE, key, "DisplayVersion");
  if (displayVersion.empty()) displayVersion = readRegString(HKEY_LOCAL_MACHINE, key, "ReleaseId");
  std::string buildNumber = readRegString(HKEY_LOCAL_MACHINE, key, "CurrentBuildNumber");
  DWORD ubr = readRegDword(HKEY_LOCAL_MACHINE, key, "UBR");

  if (productName.empty()) return "Windows (version unknown)";
  std::string result = productName;
  if (!displayVersion.empty()) result += ", " + displayVersion;
  if (!buildNumber.empty()) {
    result += " (Build " + buildNumber;
    if (ubr > 0) result += "." + std::to_string(ubr);
    result += ")";
  }
  return result;
}

#else

std::string hostOsVersion() {
  struct utsname uts {};
  std::string kernelRelease;
  std::string sysname;
  if (uname(&uts) == 0) {
    kernelRelease = uts.release;
    sysname = uts.sysname;
  }

  // /etc/os-release is the standard, distro-agnostic way to get a proper marketing
  // name ("Ubuntu 24.04.1 LTS", "Arch Linux", ...) on Linux - doesn't exist on
  // macOS, which falls back to plain uname() output below instead.
  std::string prettyName;
  std::ifstream osRelease("/etc/os-release");
  if (osRelease) {
    std::string line;
    while (std::getline(osRelease, line)) {
      if (line.rfind("PRETTY_NAME=", 0) == 0) {
        prettyName = line.substr(12);
        if (prettyName.size() >= 2 && prettyName.front() == '"' && prettyName.back() == '"') {
          prettyName = prettyName.substr(1, prettyName.size() - 2);
        }
        break;
      }
    }
  }

  if (!prettyName.empty()) {
    return kernelRelease.empty() ? prettyName : prettyName + " (kernel " + kernelRelease + ")";
  }
  if (!sysname.empty()) return kernelRelease.empty() ? sysname : sysname + " " + kernelRelease;
  return "Unknown OS";
}

#endif

}  // namespace ebl
