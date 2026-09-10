#pragma once
#include <optional>
#include <string>

namespace ebl {

/** Turns the runner's raw output lines into a single, monotonically-increasing
 * 0-100 build percentage - a direct C++ port of
 * orchestrator/src/build/progress.ts's `ProgressTracker` (the GUI path's
 * equivalent), kept in sync with it by hand if either changes, same convention as
 * detect.cpp/metrics.cpp's own header comments.
 *
 * Why this exists instead of just using build-entrypoint.sh's own `@@PROGRESS:`
 * marker directly: that marker is only ever emitted once, right as a phase
 * finishes, for most phases - and *never* during `gradle`/`eas`, the two phases
 * that actually take minutes (confirmed: no `@@PROGRESS:` call anywhere in either
 * phase's code in build-entrypoint.sh). A CLI build using only the raw marker
 * value would show the progress bar stuck at 0% for the entire gradle/eas phase.
 *
 * This blends three signals instead: (1) `@@PHASE:`/`@@PROGRESS:` markers against
 * fixed per-phase weights, so the overall percent is monotonic across phase
 * boundaries; (2) Gradle's own live `NN% EXECUTING/CONFIGURING/INITIALIZING`
 * console line - available because the build container gets a real TTY
 * (docker_client.cpp's createContainer sets Tty:true, same as
 * orchestrator/src/docker/runner.ts) and Gradle is invoked with `--console=rich`
 * (build-entrypoint.sh) specifically so this line exists; (3) a short list of
 * recognizable milestone strings in `eas build --local`'s own output, since eas-cli
 * never prints a live percentage the way Gradle does. Pure logic, no I/O - kept in
 * its own module so cli/tests/ can exercise it without Docker. */
class BuildProgressTracker {
 public:
  /** Call once the `@@ENGINE:` marker arrives - selects which per-phase weight
   * table applies and whether Gradle-console or eas-milestone parsing is active
   * during the corresponding phase. Anything other than "eas" is treated as
   * "gradle" (matches build-entrypoint.sh's own two-engine model). */
  void setEngine(const std::string& engine);

  /** Feed every line of runner output through this, marker or not - returns the
   * new overall percent if this line changed it (always >= whatever was returned
   * before), or nullopt if the line didn't affect progress at all. */
  std::optional<int> handleLine(const std::string& line);

 private:
  std::string engine_ = "gradle";
  int phaseIndex_ = 0;
  int percent_ = 0;
  size_t easMilestoneIdx_ = 0;

  int baseForCurrentPhase() const;
  int weightForCurrentPhase() const;
  int clampAdvance(double pct);
};

}  // namespace ebl
