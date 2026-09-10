#include "tui_menu.hpp"

#include <algorithm>
#include <iostream>

#include "color.hpp"
#include "tui_input.hpp"

namespace ebl {

int selectFromMenu(const std::string& title, const std::vector<std::string>& options, int defaultIndex) {
  if (options.empty()) return -1;
  int selected = std::max(0, std::min(defaultIndex, static_cast<int>(options.size()) - 1));
  int linesRendered = 0;

  auto render = [&]() {
    std::string frame = "\n" + ebl::color::bold(title) + "\n";
    for (size_t i = 0; i < options.size(); i++) {
      bool isSelected = static_cast<int>(i) == selected;
      std::string line = (isSelected ? "> " : "  ") + options[i];
      frame += (isSelected ? ebl::color::cyan(ebl::color::bold(line)) : line) + "\n";
    }
    int newLineCount = static_cast<int>(std::count(frame.begin(), frame.end(), '\n'));
    if (linesRendered > 0) std::cout << "\x1b[" << linesRendered << "A\x1b[0J";
    std::cout << frame << std::flush;
    linesRendered = newLineCount;
  };

  render();
  while (true) {
    switch (readKey()) {
      case TuiKey::Up:
        selected = (selected - 1 + static_cast<int>(options.size())) % static_cast<int>(options.size());
        render();
        break;
      case TuiKey::Down:
        selected = (selected + 1) % static_cast<int>(options.size());
        render();
        break;
      case TuiKey::Enter:
        return selected;
      case TuiKey::Escape:
        return -1;
      case TuiKey::Other:
        break;  // ignored - wait for the next key
    }
  }
}

}  // namespace ebl
