#pragma once
#include <deque>
#include <string>

namespace ebl {

/** Everything BuildStatusView needs for one frame - the caller (build.cpp) owns
 * this, updates it as markers/stats/log lines arrive, and passes it to render()
 * on each tick. History deques are oldest-first; only the most recent samples
 * that actually fit the sparkline width are shown, so callers are free to just
 * keep appending without capping length themselves (though build.cpp does cap it
 * anyway, to keep a long build's memory footprint bounded). */
struct BuildStatusState {
  std::string phaseId = "setup";
  std::string phaseLabel = "Starting...";
  int progressPercent = 0;
  long elapsedSeconds = 0;
  double cpuPercent = 0.0;
  double memUsedMb = 0.0;
  double memLimitMb = 0.0;
  std::deque<double> cpuHistory;
  std::deque<double> memPercentHistory;
  std::deque<std::string> recentLogLines;
};

/** Renders a live, redraw-in-place build-status dashboard to stdout: current
 * phase, a progress bar, elapsed time, CPU/memory current values with a small
 * auto-scaling ASCII sparkline of recent history, and a short tail of recent
 * build-tool log lines. Plain ASCII only (no Unicode block characters) -
 * deliberately, so this renders correctly on a legacy Windows console codepage
 * without needing a global UTF-8 console-output change (see main.cpp's own
 * enableAnsiOnWindowsConsole() for the equivalent reasoning around ANSI escapes
 * generally). Meant to be constructed once per build and have render() called
 * repeatedly (e.g. once a second) - it tracks how many lines the previous frame
 * used so it can move the cursor back up and clear before repainting, the same
 * technique pull_progress.cpp uses for a single line, extended to a whole block. */
class BuildStatusView {
 public:
  /** appLabel: shown once in the header (e.g. the project path being built). */
  explicit BuildStatusView(std::string appLabel);

  void render(const BuildStatusState& state);

 private:
  std::string appLabel_;
  int linesRendered_ = 0;
};

}  // namespace ebl
