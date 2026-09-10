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

std::string truncate(const std::string& s, size_t maxLen) {
  return s.size() > maxLen ? s.substr(0, maxLen) + "..." : s;
}

// Auto-scales to GB above 1024MB (a build container's memory limit is routinely
// several GB, and "4096MB" reads worse than "4.00GB") - same MB/GB-style threshold
// build.cpp's own formatBytes() already uses for KB vs. MB.
std::string formatMemory(double mb) {
  char buf[32];
  if (mb >= 1024.0) {
    std::snprintf(buf, sizeof(buf), "%.2fGB", mb / 1024.0);
  } else {
    std::snprintf(buf, sizeof(buf), "%.0fMB", mb);
  }
  return buf;
}

}  // namespace

BuildStatusView::BuildStatusView(std::string appLabel) : appLabel_(std::move(appLabel)) {}

void BuildStatusView::render(const BuildStatusState& state) {
  constexpr size_t kMaxLogLineLen = 100;

  std::ostringstream out;
  out << "\n" << ebl::color::bold("Building " + ebl::color::cyan(appLabel_)) << "\n\n";
  out << "  " << ebl::color::dim("Phase   ") << "  " << state.phaseId << " - " << state.phaseLabel << "\n";
  out << "  " << ebl::color::dim("Progress") << "  " << renderProgressBar(state.progressPercent, 30) << "\n";
  out << "  " << ebl::color::dim("Elapsed ") << "  " << formatElapsed(state.elapsedSeconds) << "\n\n";

  char cpuBuf[16];
  std::snprintf(cpuBuf, sizeof(cpuBuf), "%5.1f%%", state.cpuPercent);
  out << "  " << ebl::color::dim("CPU     ") << "  " << cpuBuf << "\n";

  double memPercent = state.memLimitMb > 0 ? (state.memUsedMb / state.memLimitMb) * 100.0 : 0.0;
  char memPctBuf[16];
  std::snprintf(memPctBuf, sizeof(memPctBuf), "%.1f%%", memPercent);
  out << "  " << ebl::color::dim("Memory  ") << "  " << formatMemory(state.memUsedMb) << " / "
      << formatMemory(state.memLimitMb) << " (" << memPctBuf << ")\n";

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
