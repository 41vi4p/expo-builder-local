#pragma once
#include <string>
#include <vector>

namespace ebl {

/** Renders `options` as a navigable, single-select menu under `title` (Up/Down to
 * move, Enter to confirm, Escape/Ctrl-C to cancel), redrawing in place after every
 * keypress - same ANSI cursor-movement technique `pull_progress.cpp`/
 * `build_status_view.cpp` already use. Blocks until the user decides. Returns the
 * chosen index, or -1 on cancellation. Needs a real terminal on both stdout and
 * stdin - callers must check that themselves first (e.g. `color::enabled()` +
 * `color::stdinIsTty()`), this doesn't re-check. */
int selectFromMenu(const std::string& title, const std::vector<std::string>& options, int defaultIndex = 0);

}  // namespace ebl
