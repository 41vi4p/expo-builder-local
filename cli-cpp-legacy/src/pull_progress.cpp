#include "pull_progress.hpp"

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include <iostream>

namespace ebl {

namespace {

bool streamIsTty(std::ostream& out) {
  // Only ever called with std::cout or std::cerr in practice - checks the fd that
  // actually matches which one was passed, not always stdout, so redirecting to
  // stderr for --json mode still gets correct TTY-vs-piped behavior.
#ifdef _WIN32
  int fd = (&out == &std::cerr) ? _fileno(stderr) : _fileno(stdout);
  return _isatty(fd) != 0;
#else
  int fd = (&out == &std::cerr) ? fileno(stderr) : fileno(stdout);
  return isatty(fd) != 0;
#endif
}

}  // namespace

PullProgressRenderer::PullProgressRenderer(std::ostream& out) : out_(out), isTty_(streamIsTty(out)) {}

void PullProgressRenderer::onEvent(const std::string& id, const std::string& status, const std::string& progress) {
  std::string text = id.empty() ? status : (id + ": " + status);
  if (!progress.empty()) text += " " + progress;

  if (!isTty_ || id.empty()) {
    out_ << text << "\n" << std::flush;
    return;
  }

  auto it = indexOf_.find(id);
  if (it == indexOf_.end()) {
    indexOf_[id] = order_.size();
    order_.push_back(text);
    out_ << text << "\n" << std::flush;
    return;
  }

  size_t idx = it->second;
  order_[idx] = text;
  size_t linesUp = order_.size() - idx;
  // Move up to the tracked line, clear it, redraw, then move back down to the
  // blank line below the last tracked one — the same spot every redraw starts from.
  out_ << "\x1b[" << linesUp << "A\r\x1b[2K" << text << "\x1b[" << linesUp << "B\r" << std::flush;
}

}  // namespace ebl
