#include "build_status_view.hpp"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <sstream>

#include "color.hpp"

namespace ebl {

namespace {

std::string formatElapsed(long seconds) {
  long m = seconds / 60;
  long s = seconds % 60;
  char buf[32];  // generously sized - GCC's -Wformat-truncation estimates %ld's
                  // worst case from `long`'s full range, not realistic elapsed times
  std::snprintf(buf, sizeof(buf), "%02ld:%02ld", m, s);
  return buf;
}

std::string renderProgressBar(int percent, int width) {
  percent = std::max(0, std::min(100, percent));
  int filled = (percent * width) / 100;
  std::string bar = "[";
  bar.append(static_cast<size_t>(filled), '=');
  bar.append(static_cast<size_t>(width - filled), '-');
  bar += "] " + std::to_string(percent) + "%";
  return bar;
}

// 8-level plain-ASCII intensity ramp, auto-scaled against the max value currently
// in `values` - so this reads sensibly whether it's CPU% (0-100, or higher with
// multiple cores) or memory% (0-100), without the caller needing to know which.
std::string renderSparkline(const std::deque<double>& values, size_t width) {
  static constexpr char kLevels[] = " .:-=+*#";
  constexpr int kLevelCount = 8;
  if (values.empty()) return std::string(width, ' ');

  double maxV = 0.0;
  for (double v : values) maxV = std::max(maxV, v);
  if (maxV <= 0.0) maxV = 1.0;

  size_t start = values.size() > width ? values.size() - width : 0;
  std::string out;
  out.reserve(width);
  for (size_t i = start; i < values.size(); i++) {
    int level = static_cast<int>((values[i] / maxV) * (kLevelCount - 1) + 0.5);
    level = std::max(0, std::min(kLevelCount - 1, level));
    out += kLevels[level];
  }
  return out;
}

std::string truncate(const std::string& s, size_t maxLen) {
  return s.size() > maxLen ? s.substr(0, maxLen) + "..." : s;
}

}  // namespace

BuildStatusView::BuildStatusView(std::string appLabel) : appLabel_(std::move(appLabel)) {}

void BuildStatusView::render(const BuildStatusState& state) {
  constexpr size_t kSparklineWidth = 40;
  constexpr size_t kMaxLogLineLen = 100;

  std::ostringstream out;
  out << "\n" << ebl::color::bold("Building " + ebl::color::cyan(appLabel_)) << "\n\n";
  out << "  " << ebl::color::dim("Phase   ") << "  " << state.phaseId << " - " << state.phaseLabel << "\n";
  out << "  " << ebl::color::dim("Progress") << "  " << renderProgressBar(state.progressPercent, 30) << "\n";
  out << "  " << ebl::color::dim("Elapsed ") << "  " << formatElapsed(state.elapsedSeconds) << "\n\n";

  char cpuBuf[16];
  std::snprintf(cpuBuf, sizeof(cpuBuf), "%5.1f%%", state.cpuPercent);
  out << "  " << ebl::color::dim("CPU     ") << "  " << cpuBuf << "  "
      << renderSparkline(state.cpuHistory, kSparklineWidth) << "\n";

  double memPercent = state.memLimitMb > 0 ? (state.memUsedMb / state.memLimitMb) * 100.0 : 0.0;
  char memBuf[64];
  std::snprintf(memBuf, sizeof(memBuf), "%.0fMB / %.0fMB (%.1f%%)", state.memUsedMb, state.memLimitMb, memPercent);
  out << "  " << ebl::color::dim("Memory  ") << "  " << memBuf << "  "
      << renderSparkline(state.memPercentHistory, kSparklineWidth) << "\n";

  if (!state.recentLogLines.empty()) {
    out << "\n";
    for (const auto& line : state.recentLogLines) {
      out << "  " << ebl::color::dim(truncate(line, kMaxLogLineLen)) << "\n";
    }
  }

  std::string frame = out.str();
  int newLineCount = static_cast<int>(std::count(frame.begin(), frame.end(), '\n'));

  if (linesRendered_ > 0) {
    // Move up to the start of the previous frame, then clear everything from
    // there to the end of the screen before repainting - a plain per-line clear
    // wouldn't erase a line that existed last frame (e.g. a log line) but isn't
    // part of this one.
    std::cout << "\x1b[" << linesRendered_ << "A\x1b[0J";
  }
  std::cout << frame << std::flush;
  linesRendered_ = newLineCount;
}

}  // namespace ebl
