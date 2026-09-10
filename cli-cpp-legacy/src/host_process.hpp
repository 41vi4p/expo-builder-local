#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ebl {

/** Runs `args[0] args[1...]` directly on the host (no Docker, no shell) with its
 * working directory set to `workingDir`, streaming its combined stdout+stderr to
 * `onChunk` live as it arrives — the same shape as `DockerClient::attachAndStream`'s
 * callback, so callers can reuse familiar line-buffering logic against it. Blocks
 * until the process exits.
 *
 * Returns the process's real exit code, or -1 if it couldn't even be spawned (in
 * which case `onChunk` is never called at all). POSIX: a failed `execvp` (e.g.
 * `npx` not on PATH) surfaces as exit code 127, the same convention every POSIX
 * shell uses for "command not found" — callers can check for that specifically to
 * give a clearer message than a raw exit code.
 *
 * Extends metrics.cpp's `runCommandCapture` pattern (same fork+execvp+pipe /
 * CreateProcess+CreatePipe skeleton) rather than replacing it — that function stays
 * as-is for its one existing buffer-then-return caller (git metadata); this one is
 * for a caller that needs live output and a real working directory instead. */
int runHostProcessStreaming(const std::vector<std::string>& args, const std::string& workingDir,
                             const std::function<void(const char*, size_t)>& onChunk);

}  // namespace ebl
