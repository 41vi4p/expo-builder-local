#pragma once
#include <string>

namespace ebl {

/** Locates the bundled Android runner build context (Dockerfile + entrypoint
 * scripts) so the CLI can build the runner image itself without the rest of the
 * expo-builder-local repo being present on disk. Throws with a clear message if it
 * can't be found anywhere sensible. */
std::string resolveRunnerContextDir();

}  // namespace ebl
