#include "prompt.hpp"

#include <termios.h>
#include <unistd.h>

#include <iostream>

#include "color.hpp"

namespace ebl {

std::string promptString(const std::string& question, const std::string& defaultValue) {
  std::cout << question;
  if (!defaultValue.empty()) std::cout << " [" << defaultValue << "]";
  std::cout << ": " << std::flush;
  std::string line;
  std::getline(std::cin, line);
  return line.empty() ? defaultValue : line;
}

int promptInt(const std::string& question, int defaultValue) {
  while (true) {
    std::string raw = promptString(question, std::to_string(defaultValue));
    try {
      return std::stoi(raw);
    } catch (const std::exception&) {
      std::cout << ebl::color::red("Enter a whole number.") << "\n";
    }
  }
}

std::string promptHidden(const std::string& question) {
  std::cout << question << ": " << std::flush;
  if (!isatty(fileno(stdin))) {
    std::string line;
    std::getline(std::cin, line);
    return line;
  }

  termios oldTerm{};
  tcgetattr(STDIN_FILENO, &oldTerm);
  termios newTerm = oldTerm;
  newTerm.c_lflag &= ~ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &newTerm);

  std::string line;
  std::getline(std::cin, line);

  tcsetattr(STDIN_FILENO, TCSANOW, &oldTerm);
  std::cout << "\n";
  return line;
}

}  // namespace ebl
