#pragma once
#include <string>

namespace ebl {

/** Dotted-integer version comparison ("0.9.0" < "0.10.0"), not a string compare -
 * tolerates a leading "v" on either side (GitHub tag_name convention, e.g.
 * "v0.22.0"). Returns true iff `a` is strictly newer than `b`. Pure and
 * dependency-free (no curl/OpenSSL/filesystem) - kept as its own tiny module
 * specifically so cli/tests/ can exercise it without pulling in update_check.cpp's
 * network/config dependencies. */
bool isVersionNewer(const std::string& a, const std::string& b);

}  // namespace ebl
