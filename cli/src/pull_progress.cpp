#include "pull_progress.hpp"

#include <unistd.h>

#include <iostream>

namespace ebl {

PullProgressRenderer::PullProgressRenderer() : isTty_(isatty(fileno(stdout)) != 0) {}

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
