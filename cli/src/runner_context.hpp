#pragma once
#include <string>

namespace ebl {

/** Locates the bundled Android runner build context (Dockerfile + entrypoint
 * scripts) so the CLI can build the runner image itself without the rest of the
 * expo-builder-local repo being present on disk. Throws with a clear message if it
 * can't be found anywhere sensible. */
std::string resolveRunnerContextDir();

/** Windows-only native build engine: locates the bundled
 * patch-android-signing.js/write-eas-credentials.js pair (see
 * docker/runner/scripts/ - the same two files the Docker-based engine uses,
 * reused unmodified) next to the installed binary, using the same lookup order as
 * resolveRunnerContextDir(). Throws with a clear message if it can't be found. */
std::string resolveNativeScriptsDir();

}  // namespace ebl
