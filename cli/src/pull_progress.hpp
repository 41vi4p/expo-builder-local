#pragma once
// Renders `docker pull`-style progress: each layer id gets a single line that's
// updated in place (cursor-up + redraw) instead of a new line scrolling past for
// every progress tick. Falls back to plain line-by-line output when stdout isn't a
// terminal (piped/redirected), same as the real `docker` CLI does.
#include <string>
#include <unordered_map>
#include <vector>

namespace ebl {

class PullProgressRenderer {
 public:
  PullProgressRenderer();

  /** id: layer short hash, or empty for a plain status line (e.g. "Status:
   * Downloaded newer image for ..."), which is always printed as its own line and
   * never redrawn in place. progress: Docker's own pre-rendered bar string (e.g.
   * "[==>       ]  1.2MB/5MB"), may be empty. */
  void onEvent(const std::string& id, const std::string& status, const std::string& progress);

 private:
  bool isTty_;
  std::vector<std::string> order_;
  std::unordered_map<std::string, size_t> indexOf_;
};

}  // namespace ebl
