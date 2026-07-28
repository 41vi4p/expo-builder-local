#include "winpath.hpp"

namespace ebl {

std::string toDockerBindPath(const std::string& hostPath) {
#ifdef _WIN32
  // Only a plain drive-letter path ("C:\..." or "C:/...") needs translating — a
  // path that's already forward-slashed and driveless (unlikely here, since every
  // caller derives from std::filesystem::absolute() on Windows, but harmless to
  // leave alone) is passed through as-is.
  if (hostPath.size() < 3 || hostPath[1] != ':' || (hostPath[2] != '\\' && hostPath[2] != '/')) {
    return hostPath;
  }

  std::string out = "//";
  char drive = hostPath[0];
  out += static_cast<char>(drive >= 'A' && drive <= 'Z' ? drive - 'A' + 'a' : drive);
  out += '/';
  for (size_t i = 3; i < hostPath.size(); i++) {
    char c = hostPath[i];
    out += (c == '\\') ? '/' : c;
  }
  return out;
#else
  return hostPath;
#endif
}

}  // namespace ebl
