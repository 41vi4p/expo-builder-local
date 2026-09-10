#pragma once
// Windows -> Docker Desktop host-path translation for bind-mount strings.
#include <string>

namespace ebl {

/** Converts a host path into the form Docker Desktop's Engine API expects as a
 * bind-mount source. On Windows, "D:\Documents\App" becomes "//d/Documents/App" —
 * the daemon runs inside Docker Desktop's own Linux VM, so a raw drive-letter path
 * means nothing to it (and its ':' would collide with the "host:container" bind
 * string's own separator besides); "//d/..." is the form Docker Desktop's
 * file-sharing layer recognizes and maps back to the real Windows path, the same
 * conversion the real `docker` CLI itself performs client-side. On every other
 * platform this is the identity function — call it unconditionally at every
 * bind-mount construction site, no #ifdef needed at the call site. */
std::string toDockerBindPath(const std::string& hostPath);

}  // namespace ebl
