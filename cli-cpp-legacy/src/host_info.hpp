#pragma once
#include <string>

namespace ebl {

/** Best-effort, human-readable description of the OS/version this binary is
 * actually running on right now (not what it was compiled for) - e.g.
 * "Windows 11 Pro, 23H2 (Build 22631.3737)" on Windows, or "Ubuntu 24.04.1 LTS
 * (kernel 6.8.0-31-generic)" on Linux. Purely informational (`ebl --version`) -
 * never throws, falls back to whatever partial info is available rather than
 * erroring out. */
std::string hostOsVersion();

}  // namespace ebl
