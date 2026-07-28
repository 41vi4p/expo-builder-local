#include "config.hpp"

#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

#include "../color.hpp"
#include "../config_store.hpp"

namespace fs = std::filesystem;

namespace ebl::commands {

namespace {

void printUsage() {
  std::cout << R"(ebl config

Interactive wizard that saves your projects folder, Expo token, and port settings to
~/.config/ebl/config.json (secrets encrypted at rest — see README). Re-run any time
to change a setting; existing values are shown as defaults.

Options:
  -h, --help   Show this help
)";
}

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

/** Reads a line with terminal echo disabled — used for the Expo token, matching the
 * usual convention for anything token/password-shaped. Falls back to a normal
 * (echoed) read if stdin isn't actually a terminal (e.g. piped input in a script). */
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

std::string maskedPreview(const std::string& secret) {
  if (secret.empty()) return "(not set)";
  if (secret.size() <= 4) return "****";
  return std::string(secret.size() - 4, '*') + secret.substr(secret.size() - 4);
}

}  // namespace

void printConfigUsage() { printUsage(); }

int runConfig(int argc, char** argv) {
  for (int i = 0; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      printUsage();
      return 0;
    }
  }

  ebl::EblConfig cfg = ebl::loadConfig().value_or(ebl::EblConfig{});

  std::cout << ebl::color::bold("ebl config") << " — press Enter to keep the value shown in [brackets].\n\n";

  while (true) {
    std::string root = promptString("Projects folder (parent directory of the Expo apps you'll build/browse)",
                                     cfg.projectsRoot);
    fs::path resolved = fs::absolute(root).lexically_normal();
    if (!fs::exists(resolved) || !fs::is_directory(resolved)) {
      std::cout << ebl::color::red("Not a directory: " + resolved.string()) << " — try again.\n";
      continue;
    }
    cfg.projectsRoot = resolved.string();
    break;
  }

  std::cout << "\n" << ebl::color::dim(
                            "Default Expo access token — only needed for the \"eas\" build engine, used when a "
                            "project's app.json has no owner-specific token below (or no \"owner\" field at "
                            "all). Create one at https://expo.dev/accounts/[account]/settings/access-tokens "
                            "(leave blank to skip).")
            << "\n";
  std::cout << ebl::color::dim("Current: " + maskedPreview(cfg.expoToken)) << "\n";
  std::string newToken = promptHidden("Default Expo access token (leave blank to keep current, type \"clear\" to remove it)");
  if (newToken == "clear") {
    cfg.expoToken.clear();
  } else if (!newToken.empty()) {
    cfg.expoToken = newToken;
  }

  std::cout << "\n" << ebl::color::dim(
                            "Per-account Expo tokens — if you build apps under more than one EAS account "
                            "(e.g. a personal account and an organization), save one token per account here. "
                            "`ebl build` auto-selects by matching the project's app.json \"owner\" field, no "
                            "need to pass --expo-token by hand.")
            << "\n";
  if (cfg.expoTokensByOwner.empty()) {
    std::cout << ebl::color::dim("Current: (none saved)") << "\n";
  } else {
    std::cout << ebl::color::dim("Current:") << "\n";
    for (const auto& entry : cfg.expoTokensByOwner) {
      std::cout << ebl::color::dim("  " + entry.owner + ": " + maskedPreview(entry.token)) << "\n";
    }
  }
  while (true) {
    std::string owner =
        promptString("Account/owner to add or update (blank to finish, \"remove <owner>\" to delete one)", "");
    if (owner.empty()) break;
    if (owner.rfind("remove ", 0) == 0) {
      std::string target = owner.substr(7);
      auto& entries = cfg.expoTokensByOwner;
      auto it = std::find_if(entries.begin(), entries.end(),
                              [&](const ebl::ExpoTokenEntry& e) { return e.owner == target; });
      if (it == entries.end()) {
        std::cout << ebl::color::red("No saved token for owner \"" + target + "\".") << "\n";
      } else {
        entries.erase(it);
        std::cout << ebl::color::green("Removed \"" + target + "\".") << "\n";
      }
      continue;
    }
    std::string token = promptHidden("Expo access token for \"" + owner + "\"");
    if (token.empty()) {
      std::cout << ebl::color::red("Empty token, not saved.") << "\n";
      continue;
    }
    auto& entries = cfg.expoTokensByOwner;
    auto it = std::find_if(entries.begin(), entries.end(),
                            [&](const ebl::ExpoTokenEntry& e) { return e.owner == owner; });
    if (it == entries.end()) {
      entries.push_back({owner, token});
    } else {
      it->token = token;
    }
    std::cout << ebl::color::green("Saved token for \"" + owner + "\".") << "\n";
  }

  std::cout << "\n";
  cfg.orchestratorPort = promptInt("Orchestrator port", cfg.orchestratorPort);
  cfg.webPort = promptInt("Web GUI port", cfg.webPort);

  std::cout << "\n" << ebl::color::dim(
                            "Docker Hub namespace — used to pull/build the runner, orchestrator, and web "
                            "images (<namespace>/expo-builder-local-*). Leave default unless you're running "
                            "your own published images.")
            << "\n";
  cfg.dockerHubNamespace = promptString("Docker Hub namespace", cfg.dockerHubNamespace);

  ebl::saveConfig(cfg);

  std::cout << "\n" << ebl::color::green(ebl::color::bold("Saved to " + ebl::configFilePath())) << "\n";
  std::cout << "  Projects folder:      " << cfg.projectsRoot << "\n";
  std::cout << "  Default Expo token:   " << maskedPreview(cfg.expoToken) << "\n";
  std::cout << "  Per-account tokens:   "
            << (cfg.expoTokensByOwner.empty() ? "(none)" : std::to_string(cfg.expoTokensByOwner.size()) + " saved")
            << "\n";
  std::cout << "  Orchestrator port:    " << cfg.orchestratorPort << "\n";
  std::cout << "  Web GUI port:         " << cfg.webPort << "\n";
  std::cout << "  Docker Hub namespace: " << cfg.dockerHubNamespace << "\n\n";
  std::cout << "Next: " << ebl::color::cyan("ebl start") << "\n";
  return 0;
}

}  // namespace ebl::commands
