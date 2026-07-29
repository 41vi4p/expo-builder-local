#include "pull_progress.hpp"

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include <iostream>

namespace ebl {

#ifdef _WIN32
PullProgressRenderer::PullProgressRenderer() : isTty_(_isatty(_fileno(stdout)) != 0) {}
#else
PullProgressRenderer::PullProgressRenderer() : isTty_(isatty(fileno(stdout)) != 0) {}
#endif

void PullProgressRenderer::onEvent(const std::string& id, const std::string& status, const std::string& progress) {
  std::string text = id.empty() ? status : (id + ": " + status);
  if (!progress.empty()) text += " " + progress;

  if (!isTty_ || id.empty()) {
    std::cout << text << "\n" << std::flush;
    return;
  }

  auto it = indexOf_.find(id);
  if (it == indexOf_.end()) {
    indexOf_[id] = order_.size();
    order_.push_back(text);
    std::cout << text << "\n" << std::flush;
    return;
  }

  size_t idx = it->second;
  order_[idx] = text;
  size_t linesUp = order_.size() - idx;
  // Move up to the tracked line, clear it, redraw, then move back down to the
  // blank line below the last tracked one — the same spot every redraw starts from.
  std::cout << "\x1b[" << linesUp << "A\r\x1b[2K" << text << "\x1b[" << linesUp << "B\r" << std::flush;
}

}  // namespace ebl
